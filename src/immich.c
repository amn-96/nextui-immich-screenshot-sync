#include <fcntl.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "immich.h"
#include "prefs.h"

#define ID_LEN 40
#define CURSOR_LEN 128

static const char* KEY_PERMISSIONS[] = {
	"asset.read", "asset.view", "asset.upload", "asset.download", "album.read", "album.create", "albumAsset.create", "user.read",
};

#define LIST_PAGE_SIZE 1000

static __thread char last_error[256];
static volatile pid_t curl_pid;
__thread int immich_untracked;

const char* immich_error(void) {
	return last_error;
}

static int fail(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(last_error, sizeof(last_error), fmt, args);
	va_end(args);
	return -1;
}

static int json_int(const cJSON* obj, const char* key) {
	const cJSON* field = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsNumber(field) ? field->valueint : 0;
}

// Leaves dest empty and returns -1 when the field is absent or null.
static int copy_json_string(char* dest, size_t size, const cJSON* obj, const char* key) {
	const cJSON* field = cJSON_GetObjectItemCaseSensitive(obj, key);
	dest[0] = '\0';
	if (!cJSON_IsString(field)) return -1;
	snprintf(dest, size, "%s", field->valuestring);
	return 0;
}

static int json_string_field(const char* json, const char* key, char* dest, size_t size) {
	cJSON* root = cJSON_Parse(json);
	int status = copy_json_string(dest, size, root, key);
	cJSON_Delete(root);
	return status ? fail("response missing \"%s\"", key) : 0;
}

static char* take_json_string(cJSON* root) {
	char* text = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return text;
}

static char* read_file(const char* path) {
	FILE* file = fopen(path, "rb");
	if (!file) return NULL;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);
	char* contents = malloc(size + 1);
	contents[fread(contents, 1, size, file)] = '\0';
	fclose(file);
	return contents;
}

// Writes value as a curl config-file quoted string.
static void put_config(FILE* config, const char* option, const char* value) {
	fprintf(config, "%s = \"", option);
	for (const char* c = value; *c; c++) {
		if (*c == '\n') fputs("\\n", config);
		else if (*c == '\r') fputs("\\r", config);
		else if (*c == '\t') fputs("\\t", config);
		else {
			if (*c == '\\' || *c == '"') fputc('\\', config);
			fputc(*c, config);
		}
	}
	fputs("\"\n", config);
}

typedef struct {
	const char* method;
	const char* path;              // relative to /api
	const char* server_url;        // NULL: prefs.server_url
	const char* bearer;            // NULL: x-api-key from prefs
	const char* json_body;
	const char** forms;            // NULL-terminated curl -F fields
	const char* dest_path;         // body goes here instead of *response
	char** response;               // malloc'd body on success
} Request;

// Secrets and bodies go through a 0600 curl config file so they never appear in argv.
static int request(const Request* req) {
	char config_path[] = "/tmp/immich-curl-XXXXXX";
	char body_path[] = "/tmp/immich-body-XXXXXX";
	FILE* config = fdopen(mkstemp(config_path), "w");
	if (!req->dest_path) close(mkstemp(body_path));

	char url[512];
	const char* base = req->server_url ? req->server_url : prefs.server_url;
	int base_len = strlen(base);
	while (base_len && base[base_len - 1] == '/') base_len--;
	snprintf(url, sizeof(url), "%.*s/api%s", base_len, base, req->path);
	fputs("silent\nconnect-timeout = 10\nwrite-out = \"%{http_code}\"\n", config);
	put_config(config, "request", req->method);
	put_config(config, "url", url);
	put_config(config, "output", req->dest_path ? req->dest_path : body_path);
	if (req->bearer) {
		char header[600];
		snprintf(header, sizeof(header), "Authorization: Bearer %s", req->bearer);
		put_config(config, "header", header);
	} else if (prefs.api_key[0]) {
		char header[160];
		snprintf(header, sizeof(header), "x-api-key: %s", prefs.api_key);
		put_config(config, "header", header);
	}
	if (req->json_body) {
		put_config(config, "header", "Content-Type: application/json");
		put_config(config, "data-binary", req->json_body);
	}
	for (const char** form = req->forms; form && *form; form++) put_config(config, "form", *form);
	fclose(config);

	int status_pipe[2];
	pipe(status_pipe);
	pid_t pid = fork();
	if (!pid) {
		dup2(status_pipe[1], STDOUT_FILENO);
		dup2(open("/dev/null", O_WRONLY), STDERR_FILENO);
		execlp("curl", "curl", "-K", config_path, (char*)NULL);
		_exit(127);
	}
	close(status_pipe[1]);
	if (!immich_untracked) curl_pid = pid;
	FILE* curl = fdopen(status_pipe[0], "r");
	char status_text[16] = "";
	fgets(status_text, sizeof(status_text), curl);
	fclose(curl);
	waitpid(pid, NULL, 0);
	if (!immich_untracked) curl_pid = 0;
	unlink(config_path);

	int http_status = atoi(status_text);
	char* body = req->dest_path ? NULL : read_file(body_path);
	if (!req->dest_path) unlink(body_path);

	if (http_status < 200 || http_status > 299) {
		char message[160] = "";
		cJSON* root = body ? cJSON_Parse(body) : NULL;
		copy_json_string(message, sizeof(message), root, "message");
		cJSON_Delete(root);
		free(body);
		if (req->dest_path) unlink(req->dest_path);
		return http_status ? fail("HTTP %d %s", http_status, message) : fail("no response from server");
	}
	if (req->response) *req->response = body;
	else free(body);
	return 0;
}

int immich_parse_albums(const char* json, ImmichAlbum** albums, int* count) {
	cJSON* root = cJSON_Parse(json);
	if (!cJSON_IsArray(root)) {
		cJSON_Delete(root);
		return fail("unexpected album list response");
	}
	*count = cJSON_GetArraySize(root);
	*albums = calloc(*count + 1, sizeof(ImmichAlbum));
	int i = 0;
	for (const cJSON* album = root->child; album; album = album->next, i++) {
		copy_json_string((*albums)[i].id, ID_LEN, album, "id");
		copy_json_string((*albums)[i].name, sizeof((*albums)[i].name), album, "albumName");
		(*albums)[i].asset_count = json_int(album, "assetCount");
		copy_json_string((*albums)[i].newest_photo_at, sizeof((*albums)[i].newest_photo_at), album, "endDate");
		copy_json_string((*albums)[i].updated_at, sizeof((*albums)[i].updated_at), album, "lastModifiedAssetTimestamp");
		copy_json_string((*albums)[i].cover_id, ID_LEN, album, "albumThumbnailAssetId");
	}
	cJSON_Delete(root);
	return 0;
}

int immich_parse_assets(const char* json, ImmichAsset** assets, int* count, char next_cursor_out[CURSOR_LEN]) {
	cJSON* root = cJSON_Parse(json);
	const cJSON* page = cJSON_GetObjectItemCaseSensitive(root, "assets");
	const cJSON* page_items = cJSON_GetObjectItemCaseSensitive(page, "items");
	if (!cJSON_IsArray(page_items)) {
		cJSON_Delete(root);
		return fail("unexpected search response");
	}
	*count = cJSON_GetArraySize(page_items);
	*assets = calloc(*count + 1, sizeof(ImmichAsset));
	int i = 0;
	for (const cJSON* asset = page_items->child; asset; asset = asset->next, i++) {
		copy_json_string((*assets)[i].id, ID_LEN, asset, "id");
		copy_json_string((*assets)[i].filename, sizeof((*assets)[i].filename), asset, "originalFileName");
		copy_json_string((*assets)[i].created_at, sizeof((*assets)[i].created_at), asset, "fileCreatedAt");
		(*assets)[i].width = json_int(asset, "width");
		(*assets)[i].height = json_int(asset, "height");
		const cJSON* file_size = cJSON_GetObjectItemCaseSensitive(
			cJSON_GetObjectItemCaseSensitive(asset, "exifInfo"), "fileSizeInByte");
		(*assets)[i].size_bytes = cJSON_IsNumber(file_size) ? (long long)file_size->valuedouble : 0;
	}
	copy_json_string(next_cursor_out, CURSOR_LEN, page, "nextCursor");
	cJSON_Delete(root);
	return 0;
}

char* immich_albums_to_json(const ImmichAlbum* albums, int count) {
	cJSON* root = cJSON_CreateArray();
	for (int i = 0; i < count; i++) {
		cJSON* album = cJSON_CreateObject();
		cJSON_AddItemToArray(root, album);
		cJSON_AddStringToObject(album, "id", albums[i].id);
		cJSON_AddStringToObject(album, "albumName", albums[i].name);
		cJSON_AddNumberToObject(album, "assetCount", albums[i].asset_count);
		cJSON_AddStringToObject(album, "endDate", albums[i].newest_photo_at);
		cJSON_AddStringToObject(album, "lastModifiedAssetTimestamp", albums[i].updated_at);
		cJSON_AddStringToObject(album, "albumThumbnailAssetId", albums[i].cover_id);
	}
	return take_json_string(root);
}

char* immich_assets_to_json(const ImmichAsset* assets, int count, const char* next_cursor) {
	cJSON* root = cJSON_CreateObject();
	cJSON* page = cJSON_AddObjectToObject(root, "assets");
	cJSON* page_items = cJSON_AddArrayToObject(page, "items");
	for (int i = 0; i < count; i++) {
		cJSON* asset = cJSON_CreateObject();
		cJSON_AddStringToObject(asset, "id", assets[i].id);
		cJSON_AddStringToObject(asset, "originalFileName", assets[i].filename);
		cJSON_AddStringToObject(asset, "fileCreatedAt", assets[i].created_at);
		cJSON_AddNumberToObject(asset, "width", assets[i].width);
		cJSON_AddNumberToObject(asset, "height", assets[i].height);
		cJSON_AddNumberToObject(cJSON_AddObjectToObject(asset, "exifInfo"), "fileSizeInByte", (double)assets[i].size_bytes);
		cJSON_AddItemToArray(page_items, asset);
	}
	if (*next_cursor) cJSON_AddStringToObject(page, "nextCursor", next_cursor);
	return take_json_string(root);
}

// Each check id is the file path, so results are matched back by path.
static int parse_bulk_check(const char* json, const char** paths, int count, int* accept_out, char (*duplicate_ids_out)[ID_LEN]) {
	cJSON* root = cJSON_Parse(json);
	const cJSON* checks = cJSON_GetObjectItemCaseSensitive(root, "results");
	if (!cJSON_IsArray(checks)) {
		cJSON_Delete(root);
		return fail("unexpected bulk-upload-check response");
	}
	for (int i = 0; i < count; i++) {
		accept_out[i] = 0;
		duplicate_ids_out[i][0] = '\0';
	}
	for (const cJSON* check = checks->child; check; check = check->next) {
		const cJSON* id = cJSON_GetObjectItemCaseSensitive(check, "id");
		const cJSON* action = cJSON_GetObjectItemCaseSensitive(check, "action");
		if (!cJSON_IsString(id) || !cJSON_IsString(action)) continue;
		int accepted = !strcmp(action->valuestring, "accept");
		for (int i = 0; i < count; i++) {
			if (strcmp(paths[i], id->valuestring)) continue;
			accept_out[i] = accepted;
			copy_json_string(duplicate_ids_out[i], ID_LEN, check, "assetId");
		}
	}
	cJSON_Delete(root);
	return 0;
}

int immich_ping(void) {
	return request(&(Request){.method = "GET", .path = "/server/ping"});
}

int immich_server_version(char version_out[32]) {
	char* response = NULL;
	if (request(&(Request){.method = "GET", .path = "/server/version", .response = &response})) return -1;
	cJSON* root = cJSON_Parse(response);
	snprintf(version_out, 32, "v%d.%d.%d", json_int(root, "major"), json_int(root, "minor"), json_int(root, "patch"));
	cJSON_Delete(root);
	free(response);
	return 0;
}

int immich_user_name(char name_out[128]) {
	char* response = NULL;
	if (request(&(Request){.method = "GET", .path = "/users/me", .response = &response})) return -1;
	int status = json_string_field(response, "name", name_out, 128);
	free(response);
	return status;
}

int immich_login(const char* server_url, const char* email, const char* password) {
	cJSON* login = cJSON_CreateObject();
	cJSON_AddStringToObject(login, "email", email);
	cJSON_AddStringToObject(login, "password", password);
	char* login_body = take_json_string(login);
	char* login_response = NULL;
	int status = request(&(Request){.method = "POST", .path = "/auth/login", .server_url = server_url,
		.json_body = login_body, .response = &login_response});
	free(login_body);
	if (status) return -1;

	char access_token[512];
	status = json_string_field(login_response, "accessToken", access_token, sizeof(access_token));
	free(login_response);
	if (status) return -1;

	char hostname[64] = "";
	gethostname(hostname, sizeof(hostname) - 1);
	char key_name[96];
	snprintf(key_name, sizeof(key_name), "NextUI %s", hostname[0] ? hostname : "Brick");
	cJSON* key = cJSON_CreateObject();
	cJSON_AddStringToObject(key, "name", key_name);
	cJSON* permissions = cJSON_AddArrayToObject(key, "permissions");
	for (size_t i = 0; i < sizeof(KEY_PERMISSIONS) / sizeof(*KEY_PERMISSIONS); i++) {
		cJSON_AddItemToArray(permissions, cJSON_CreateString(KEY_PERMISSIONS[i]));
	}
	char* key_body = take_json_string(key);
	char* key_response = NULL;
	status = request(&(Request){.method = "POST", .path = "/api-keys", .server_url = server_url,
		.bearer = access_token, .json_body = key_body, .response = &key_response});
	free(key_body);
	if (status) return -1;

	status = json_string_field(key_response, "secret", prefs.api_key, sizeof(prefs.api_key));
	free(key_response);
	if (status) return -1;

	snprintf(prefs.server_url, sizeof(prefs.server_url), "%s", server_url);
	snprintf(prefs.user_email, sizeof(prefs.user_email), "%s", email);
	prefs_save();
	return 0;
}

int immich_list_albums(ImmichAlbum** albums, int* count) {
	char* response = NULL;
	if (request(&(Request){.method = "GET", .path = "/albums", .response = &response})) return -1;
	int status = immich_parse_albums(response, albums, count);
	free(response);
	return status;
}

int immich_ensure_album(const char* name, char id_out[40]) {
	ImmichAlbum* albums;
	int count;
	if (immich_list_albums(&albums, &count)) return -1;
	for (int i = 0; i < count; i++) {
		if (!strcmp(albums[i].name, name)) {
			snprintf(id_out, ID_LEN, "%s", albums[i].id);
			free(albums);
			return 0;
		}
	}
	free(albums);

	cJSON* album = cJSON_CreateObject();
	cJSON_AddStringToObject(album, "albumName", name);
	char* body = take_json_string(album);
	char* response = NULL;
	int status = request(&(Request){.method = "POST", .path = "/albums", .json_body = body, .response = &response});
	free(body);
	if (status) return -1;
	status = json_string_field(response, "id", id_out, ID_LEN);
	free(response);
	return status;
}

int immich_album_assets(const char** album_ids, int album_count, const char* cursor, int page_size,
	ImmichAsset** assets, int* count, char next_cursor_out[128]) {
	cJSON* search = cJSON_CreateObject();
	cJSON* any_album = cJSON_AddArrayToObject(
		cJSON_AddObjectToObject(cJSON_AddObjectToObject(search, "filter"), "albumIds"), "any");
	for (int i = 0; i < album_count; i++) cJSON_AddItemToArray(any_album, cJSON_CreateString(album_ids[i]));
	cJSON* order_by = cJSON_AddObjectToObject(search, "orderBy");
	cJSON_AddStringToObject(order_by, "field", "fileCreatedAt");
	cJSON_AddStringToObject(order_by, "direction", "desc");
	cJSON_AddTrueToObject(search, "withExif");
	cJSON_AddNumberToObject(search, "size", page_size);
	if (cursor && *cursor) cJSON_AddStringToObject(search, "cursor", cursor);
	char* body = take_json_string(search);

	char* response = NULL;
	int status = request(&(Request){.method = "POST", .path = "/search/metadata", .json_body = body, .response = &response});
	free(body);
	if (status) return -1;
	status = immich_parse_assets(response, assets, count, next_cursor_out);
	free(response);
	return status;
}

int immich_album_assets_all(const char** album_ids, int album_count, ImmichAsset** assets, int* count) {
	*assets = NULL;
	*count = 0;
	char cursor[CURSOR_LEN] = "", next_cursor[CURSOR_LEN];
	do {
		ImmichAsset* page_assets;
		int page_count;
		if (immich_album_assets(album_ids, album_count, cursor, LIST_PAGE_SIZE, &page_assets, &page_count, next_cursor)) {
			free(*assets);
			return -1;
		}
		*assets = realloc(*assets, (*count + page_count + 1) * sizeof(**assets));
		memcpy(*assets + *count, page_assets, page_count * sizeof(**assets));
		*count += page_count;
		free(page_assets);
		strcpy(cursor, next_cursor);
	} while (cursor[0]);
	return 0;
}

static int download_to(const char* path, const char* dest_path) {
	char part_path[512];
	snprintf(part_path, sizeof(part_path), "%s.part", dest_path);
	if (request(&(Request){.method = "GET", .path = path, .dest_path = part_path})) return -1;
	return rename(part_path, dest_path) ? fail("cannot write %s", dest_path) : 0;
}

int immich_download_image(const char* asset_id, const char* size, const char* dest_path) {
	char path[128];
	snprintf(path, sizeof(path), "/assets/%s/thumbnail?size=%s", asset_id, size);
	return download_to(path, dest_path);
}

int immich_download_original(const char* asset_id, const char* dest_path) {
	char path[96];
	snprintf(path, sizeof(path), "/assets/%s/original", asset_id);
	return download_to(path, dest_path);
}

void immich_abort_request(void) {
	pid_t pid = curl_pid;
	if (pid) kill(pid, SIGTERM);
}

int immich_bulk_upload_check(const char** paths, const char** sha1_hex, int count, int* accept_out, char (*duplicate_ids_out)[40]) {
	cJSON* check = cJSON_CreateObject();
	cJSON* checks = cJSON_AddArrayToObject(check, "assets");
	for (int i = 0; i < count; i++) {
		cJSON* entry = cJSON_CreateObject();
		cJSON_AddStringToObject(entry, "id", paths[i]);
		cJSON_AddStringToObject(entry, "checksum", sha1_hex[i]);
		cJSON_AddItemToArray(checks, entry);
	}
	char* body = take_json_string(check);
	char* response = NULL;
	int status = request(&(Request){.method = "POST", .path = "/assets/bulk-upload-check", .json_body = body, .response = &response});
	free(body);
	if (status) return -1;
	status = parse_bulk_check(response, paths, count, accept_out, duplicate_ids_out);
	free(response);
	return status;
}

int immich_upload(const char* path, char asset_id_out[40]) {
	struct stat file_stat;
	if (stat(path, &file_stat)) return fail("cannot stat %s", path);

	char path_copy[512], asset_field[560], created_field[64], modified_field[64], filename_field[560];
	char iso_time[32];
	strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%S.000Z", gmtime(&file_stat.st_mtime));
	snprintf(path_copy, sizeof(path_copy), "%s", path);
	snprintf(asset_field, sizeof(asset_field), "assetData=@\"%s\"", path);
	snprintf(filename_field, sizeof(filename_field), "filename=%s", basename(path_copy));
	snprintf(created_field, sizeof(created_field), "fileCreatedAt=%s", iso_time);
	snprintf(modified_field, sizeof(modified_field), "fileModifiedAt=%s", iso_time);
	const char* forms[] = {asset_field, created_field, modified_field, filename_field, NULL};

	char* response = NULL;
	if (request(&(Request){.method = "POST", .path = "/assets", .forms = forms, .response = &response})) return -1;
	int status = json_string_field(response, "id", asset_id_out, ID_LEN);
	free(response);
	return status;
}

int immich_add_to_album(const char* album_id, const char** asset_ids, int count) {
	cJSON* add = cJSON_CreateObject();
	cJSON* ids = cJSON_AddArrayToObject(add, "ids");
	for (int i = 0; i < count; i++) cJSON_AddItemToArray(ids, cJSON_CreateString(asset_ids[i]));
	char* body = take_json_string(add);
	char path[96];
	snprintf(path, sizeof(path), "/albums/%s/assets", album_id);
	int status = request(&(Request){.method = "PUT", .path = path, .json_body = body});
	free(body);
	return status;
}
