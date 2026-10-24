#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#define SDCARD_ROOT "/tmp/immich-test-sdcard"
#include "../src/immich.c"
#include "../src/prefs.c"
#include "../src/cache.c"
#include "../src/sync.c"
#include "../src/daemon.c"

static char* load_fixture(const char* name) {
	char path[256];
	snprintf(path, sizeof(path), "tests/fixtures/%s", name);
	char* contents = read_file(path);
	assert(contents);
	return contents;
}

static void test_prefs_round_trip(void) {
	char dir[] = "/tmp/immich-test-XXXXXX";
	assert(mkdtemp(dir));
	char userdata[256];
	snprintf(userdata, sizeof(userdata), "%s/a/b", dir);
	setenv("SHARED_USERDATA_PATH", userdata, 1);

	prefs_load();
	assert(!prefs.album_label[0] && prefs.cache_limit_mb == 2048 && prefs.extra_album_count == 0);
	struct stat dir_stat;
	assert(!stat(prefs_dir(), &dir_stat) && S_ISDIR(dir_stat.st_mode));

	strcpy(prefs.server_url, "https://photos.example.com");
	strcpy(prefs.api_key, "k=with=equals");
	strcpy(prefs.album_label, "Pocket");
	prefs.cache_limit_mb = 0;
	prefs.background_sync = 1;
	prefs.extra_album_count = 2;
	strcpy(prefs.extra_album_ids[0], "id-one");
	strcpy(prefs.extra_album_ids[1], "id-two");
	prefs_save();

	memset(&prefs, 0, sizeof(prefs));
	prefs_load();
	assert(!strcmp(prefs.server_url, "https://photos.example.com") && !strcmp(prefs.api_key, "k=with=equals"));
	assert(!strcmp(prefs.album_label, "Pocket") && prefs.cache_limit_mb == 0 && prefs.background_sync == 1);
	assert(prefs.extra_album_count == 2 && !strcmp(prefs.extra_album_ids[1], "id-two"));
}

static void test_immich_parse_albums(void) {
	char* json = load_fixture("albums.json");
	ImmichAlbum* albums;
	int count;
	assert(!immich_parse_albums(json, &albums, &count) && count == 2);
	assert(!strcmp(albums[0].name, "Brick (NextUI)") && albums[0].asset_count == 42);
	assert(!strcmp(albums[1].id, "22222222-2222-4222-8222-222222222222"));
	free(albums);
	assert(immich_parse_albums("{\"message\":\"x\"}", &albums, &count) == -1);
	free(json);
}

static void test_parse_search_pages(void) {
	ImmichAsset* assets;
	int count;
	char next_cursor[CURSOR_LEN];

	char* json = load_fixture("search_page.json");
	assert(!immich_parse_assets(json, &assets, &count, next_cursor) && count == 2);
	assert(!strcmp(next_cursor, "abc123cursor"));
	assert(!strcmp(assets[0].filename, "Screenshot_1.png") && assets[0].width == 1024 && assets[0].height == 768);
	assert(!strcmp(assets[1].created_at, "2026-09-30T08:30:00.000Z"));
	free(assets);
	free(json);

	json = load_fixture("search_last_page.json");
	assert(!immich_parse_assets(json, &assets, &count, next_cursor) && count == 1);
	assert(next_cursor[0] == '\0' && assets[0].width == 0);
	free(assets);
	free(json);
}

static void test_parse_bulk_check(void) {
	char* json = load_fixture("bulk_check.json");
	const char* paths[] = {"/shots/old.png", "/shots/new.png", "/shots/unknown.png"};
	int accept[3] = {9, 9, 9};
	char duplicate_ids[3][40];
	assert(!parse_bulk_check(json, paths, 3, accept, duplicate_ids));
	assert(accept[0] == 0 && accept[1] == 1 && accept[2] == 0);
	assert(!strcmp(duplicate_ids[0], "dddddddd-dddd-4ddd-8ddd-dddddddddddd") && !duplicate_ids[1][0] && !duplicate_ids[2][0]);
	free(json);
}

static void test_json_string_field(void) {
	char value[64];
	char* json = load_fixture("login.json");
	assert(!json_string_field(json, "accessToken", value, sizeof(value)) && !strcmp(value, "tok_abc123"));
	assert(json_string_field(json, "secret", value, sizeof(value)) == -1);
	assert(strstr(immich_error(), "secret"));
	free(json);
	json = load_fixture("api_key.json");
	assert(!json_string_field(json, "secret", value, sizeof(value)) && !strcmp(value, "sekrit_key_value"));
	free(json);
}

static void test_client_with_fake_curl(void) {
	setenv("PATH", "tests/fake_bin:/usr/bin:/bin", 1);
	setenv("FAKE_CURL_LOG", "/tmp/immich-fake-curl.log", 1);
	strcpy(prefs.server_url, "https://photos.example.com");
	strcpy(prefs.api_key, "secret-key-123");

	assert(!immich_ping());
	char* config = read_file("/tmp/immich-fake-curl.log");
	assert(strstr(config, "url = \"https://photos.example.com/api/server/ping\""));
	assert(strstr(config, "x-api-key: secret-key-123"));
	free(config);

	ImmichAlbum* albums;
	int count;
	assert(!immich_list_albums(&albums, &count) && count == 2);
	free(albums);
	char album_id[40];
	assert(!immich_ensure_album("Holiday", album_id) && !strcmp(album_id, "22222222-2222-4222-8222-222222222222"));

	const char* album_ids[] = {"11111111-1111-4111-8111-111111111111"};
	ImmichAsset* assets;
	char next_cursor[128];
	assert(!immich_album_assets(album_ids, 1, "prev\"cursor", 50, &assets, &count, next_cursor) && count == 2);
	free(assets);
	config = read_file("/tmp/immich-fake-curl.log");
	assert(strstr(config, "\\\"albumIds\\\":{\\\"any\\\":[\\\"11111111-1111-4111-8111-111111111111\\\"]}"));
	assert(strstr(config, "\\\"cursor\\\":\\\"prev\\\\\\\"cursor\\\""));
	assert(strstr(config, "\\\"direction\\\":\\\"desc\\\""));
	free(config);

	const char* paths[] = {"/shots/old.png", "/shots/new.png"};
	const char* sha1s[] = {"aa", "bb"};
	int accept[2];
	char duplicate_ids[2][40];
	assert(!immich_bulk_upload_check(paths, sha1s, 2, accept, duplicate_ids) && !accept[0] && accept[1]);
	assert(duplicate_ids[0][0] == 'd');

	char thumb_path[] = "/tmp/immich-thumb-test.webp";
	assert(!immich_download_image("aaaaaaaa", "thumbnail", thumb_path));
	char* thumb = read_file(thumb_path);
	assert(!strcmp(thumb, "IMG"));
	free(thumb);
	unlink(thumb_path);

	assert(!immich_upload("tests/fixtures/albums.json", album_id) && !strcmp(album_id, "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"));
	config = read_file("/tmp/immich-fake-curl.log");
	assert(strstr(config, "form = \"assetData=@\\\"tests/fixtures/albums.json\\\"\""));
	assert(strstr(config, "filename=albums.json") && strstr(config, "fileCreatedAt=20"));
	free(config);

	const char* asset_ids[] = {"x"};
	assert(!immich_add_to_album(album_id, asset_ids, 1));

	setenv("FAKE_CURL_FAIL", "1", 1);
	assert(immich_ping() == -1 && !strcmp(immich_error(), "HTTP 401 Invalid API key"));
	unsetenv("FAKE_CURL_FAIL");
}

static void test_login(void) {
	char dir[] = "/tmp/immich-test-XXXXXX";
	assert(mkdtemp(dir));
	setenv("SHARED_USERDATA_PATH", dir, 1);
	prefs_load();
	assert(!immich_login("https://photos.example.com", "me@example.com", "pw\"with quote"));
	char* config = read_file("/tmp/immich-fake-curl.log");
	assert(strstr(config, "Authorization: Bearer tok_abc123"));
	assert(strstr(config, "\\\"permissions\\\":[\\\"asset.read\\\""));
	assert(strstr(config, "asset.download"));
	free(config);

	memset(&prefs, 0, sizeof(prefs));
	prefs_load();
	assert(!strcmp(prefs.api_key, "sekrit_key_value") && !strcmp(prefs.user_email, "me@example.com"));
	assert(!strcmp(prefs.server_url, "https://photos.example.com"));
}

static void use_fresh_cache(void) {
	char dir[] = "/tmp/immich-test-XXXXXX";
	assert(mkdtemp(dir));
	setenv("SHARED_USERDATA_PATH", dir, 1);
	prefs_load();
	cache_clear("");
}

// Sparse file of size_mb whose mtime is mtime_s, registered like a finished download.
static void add_cached_file(CacheKind kind, const char* key, int size_mb, time_t mtime_s) {
	char dir[300], path[400];
	cache_dir(dir, sizeof(dir), kind);
	mkdir_p(dir);
	cache_path(path, sizeof(path), kind, key);
	FILE* file = fopen(path, "w");
	assert(file && !ftruncate(fileno(file), (off_t)size_mb << 20));
	fclose(file);
	utimes(path, (struct timeval[]){{mtime_s, 0}, {mtime_s, 0}});
	cache_added(path);
}

static void test_cache_evicts_previews_by_mtime_before_thumbs(void) {
	use_fresh_cache();
	prefs.cache_limit_mb = 4;
	add_cached_file(CACHE_THUMB, "t-old", 1, 100);
	add_cached_file(CACHE_THUMB, "t-new", 1, 200);
	add_cached_file(CACHE_PREVIEW, "p-a", 1, 300);
	add_cached_file(CACHE_PREVIEW, "p-b", 1, 400);
	assert(cache_bytes() == 4LL << 20);

	cache_touch(CACHE_PREVIEW, "p-a");
	add_cached_file(CACHE_PREVIEW, "p-c", 1, 500);
	assert(!cache_exists(CACHE_PREVIEW, "p-b") && cache_exists(CACHE_PREVIEW, "p-a"));
	assert(cache_exists(CACHE_THUMB, "t-old") && cache_exists(CACHE_THUMB, "t-new"));
	assert(cache_bytes() == 4LL << 20 && cache_bytes() == scan_total());

	prefs.cache_limit_mb = 1;
	add_cached_file(CACHE_PREVIEW, "p-d", 1, 600);
	assert(!cache_exists(CACHE_PREVIEW, "p-a") && !cache_exists(CACHE_PREVIEW, "p-c") && !cache_exists(CACHE_PREVIEW, "p-d"));
	assert(!cache_exists(CACHE_THUMB, "t-old") && cache_exists(CACHE_THUMB, "t-new"));
	assert(cache_bytes() == 1LL << 20 && cache_bytes() == scan_total());

	prefs.cache_limit_mb = 0;
	add_cached_file(CACHE_PREVIEW, "p-e", 8, 700);
	assert(cache_exists(CACHE_PREVIEW, "p-e") && cache_bytes() == 9LL << 20);
	cache_clear("/preview");
	assert(cache_bytes() == 1LL << 20);
}

static void test_list_json_round_trip(void) {
	ImmichAsset assets[2] = {
		{"id-1", "Screenshot \"1\".png", "2026-09-30T09:00:00.000Z", 1024, 768, 5000000000LL},
		{"id-2", "b.png", "2026-09-30T08:30:00.000Z", 0, 0, 0},
	};
	char* json = immich_assets_to_json(assets, 2, "next\"cursor");
	ImmichAsset* parsed;
	int count;
	char next_cursor[128];
	assert(!immich_parse_assets(json, &parsed, &count, next_cursor) && count == 2);
	assert(!strcmp(next_cursor, "next\"cursor"));
	assert(!strcmp(parsed[0].filename, assets[0].filename) && parsed[0].width == 1024 && parsed[0].size_bytes == 5000000000LL && !strcmp(parsed[1].created_at, assets[1].created_at));
	free(parsed);
	free(json);
	json = immich_assets_to_json(assets, 1, "");
	assert(!immich_parse_assets(json, &parsed, &count, next_cursor) && count == 1 && !next_cursor[0]);
	free(parsed);
	free(json);

	ImmichAlbum albums[1] = {{"album-1", "Brick (NextUI)", 42}};
	ImmichAlbum* parsed_albums;
	json = immich_albums_to_json(albums, 1);
	assert(!immich_parse_albums(json, &parsed_albums, &count) && count == 1);
	assert(!strcmp(parsed_albums[0].name, "Brick (NextUI)") && parsed_albums[0].asset_count == 42);
	free(parsed_albums);
	free(json);
}

static void test_cache_lists_and_offline_albums(void) {
	use_fresh_cache();
	assert(!cache_read_list("missing"));
	cache_write_list("page-1", "{\"a\":1}");
	char* json = cache_read_list("page-1");
	assert(!strcmp(json, "{\"a\":1}"));
	free(json);

	ImmichAlbum* albums;
	int count;
	assert(cache_list_albums(&albums, &count) == 0 && count == 2);
	free(albums);
	setenv("FAKE_CURL_FAIL", "1", 1);
	assert(cache_list_albums(&albums, &count) == 1 && count == 2 && !strcmp(albums[0].name, "Brick (NextUI)"));
	free(albums);
	cache_clear("/lists");
	assert(cache_list_albums(&albums, &count) == -1);
	unsetenv("FAKE_CURL_FAIL");

	assert(!cache_download(CACHE_THUMB, "aaaaaaaa") && cache_exists(CACHE_THUMB, "aaaaaaaa") && cache_bytes() == 3);
	assert(is_match_album("Brick (NextUI)") && !is_match_album("Holiday"));
}

static void test_full_list_cache_and_slices(void) {
	use_fresh_cache();
	const char* album_ids[] = {"11111111-1111-4111-8111-111111111111"};
	char key[64], other_key[64];
	cache_list_key(album_ids, 1, key, sizeof(key));
	const char* other_ids[] = {"22222222-2222-4222-8222-222222222222"};
	cache_list_key(other_ids, 1, other_key, sizeof(other_key));
	assert(strcmp(key, other_key));

	ImmichAsset* assets;
	int total;
	assert(cache_load_assets(key, &assets, &total) == -1);
	assert(!cache_refresh_assets(album_ids, 1, key, &assets, &total) && total == 3);
	assert(assets[0].size_bytes == 123456 && !strcmp(assets[2].filename, "Screenshot_3.png"));
	char* config = read_file("/tmp/immich-fake-curl.log");
	assert(strstr(config, "\\\"withExif\\\":true") && strstr(config, "\\\"size\\\":1000"));
	free(config);

	int count;
	free(assets);

	setenv("FAKE_CURL_FAIL", "1", 1);
	assert(cache_refresh_assets(album_ids, 1, key, &assets, &count) == -1);
	unsetenv("FAKE_CURL_FAIL");
	assert(!cache_load_assets(key, &assets, &count) && count == 3 && assets[0].size_bytes == 123456);
	assert(!strcmp(assets[1].id, "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"));
	free(assets);
}

static void test_original_destination_and_detection(void) {
	use_fresh_cache();
	const char* nextui_id[] = {"11111111-1111-4111-8111-111111111111"};
	const char* both_ids[] = {"11111111-1111-4111-8111-111111111111", "22222222-2222-4222-8222-222222222222"};
	assert(!albums_all_nextui(nextui_id, 1));
	ImmichAlbum* albums;
	int count;
	assert(cache_list_albums(&albums, &count) == 0);
	free(albums);
	assert(albums_all_nextui(nextui_id, 1) && !albums_all_nextui(both_ids, 2));
	const char* unknown_id[] = {"nope"};
	assert(!albums_all_nextui(unknown_id, 1));
	assert(!strcmp(original_dir(1), SDCARD_ROOT "/Screenshots") && !strcmp(original_dir(0), SDCARD_ROOT "/Pictures/Immich"));

	mkdir_p(original_dir(1));
	mkdir_p(original_dir(0));
	char path[400];
	unlink(SDCARD_ROOT "/Screenshots/shot.png");
	unlink(SDCARD_ROOT "/Pictures/Immich/shot.png");
	assert(!original_find("shot.png", path, sizeof(path)));

	char dest[400];
	snprintf(dest, sizeof(dest), "%s/shot.png", original_dir(0));
	assert(!immich_download_original("aaaaaaaa", dest) && access(dest, F_OK) == 0);
	char* body = read_file(dest);
	assert(!strcmp(body, "ORIGINAL-BYTES"));
	free(body);
	char part[410];
	snprintf(part, sizeof(part), "%s.part", dest);
	assert(access(part, F_OK) && original_find("shot.png", path, sizeof(path)) && !strcmp(path, dest));

	FILE* screenshot = fopen(SDCARD_ROOT "/Screenshots/shot.png", "w");
	fclose(screenshot);
	assert(original_find("shot.png", path, sizeof(path)) && strstr(path, "/Screenshots/"));
	unlink(SDCARD_ROOT "/Screenshots/shot.png");
	unlink(dest);
	assert(!original_find("shot.png", path, sizeof(path)));

	setenv("FAKE_CURL_FAIL", "1", 1);
	assert(immich_download_original("aaaaaaaa", dest) == -1 && access(part, F_OK));
	unsetenv("FAKE_CURL_FAIL");
}

static void write_file(const char* path, const char* text) {
	FILE* file = fopen(path, "w");
	assert(file);
	fputs(text, file);
	fclose(file);
}

static void set_mtime(const char* path, time_t mtime) {
	struct timeval times[2] = {{mtime, 0}, {mtime, 0}};
	assert(!utimes(path, times));
}

static void use_fresh_userdata(void) {
	char dir[] = "/tmp/immich-test-XXXXXX";
	assert(mkdtemp(dir));
	setenv("SHARED_USERDATA_PATH", dir, 1);
	prefs_load();
}

static void test_merge_local_screenshots(void) {
	mkdir_p(original_dir(1));
	system("rm -f " SDCARD_ROOT "/Screenshots/*");
	const char* names[] = {"on_server.png", "old.png", "new.JPG", "notes.txt"};
	time_t mtimes[] = {1700000000, 1700000100, 1800000000};
	for (int i = 0; i < 4; i++) {
		char path[400];
		snprintf(path, sizeof(path), "%s/%s", original_dir(1), names[i]);
		FILE* file = fopen(path, "w");
		fclose(file);
		if (i < 3) utimes(path, (struct timeval[]){{mtimes[i], 0}, {mtimes[i], 0}});
	}
	ImmichAsset server[2] = {
		{.id = "srv-newest", .filename = "x.png", .created_at = "2030-01-01T00:00:00.000Z"},
		{.id = "srv-dup", .filename = "on_server.png", .created_at = "2023-11-14T22:13:20.000Z", .width = 10},
	};
	int count;
	ImmichAsset* merged = merge_local_screenshots(server, 2, &count);
	assert(count == 4);
	assert(!strcmp(merged[0].id, "srv-newest") && !strcmp(merged[1].filename, "new.JPG") && !strcmp(merged[1].created_at, "2027-01-15T08:00:00.000Z"));
	assert(!strcmp(merged[2].filename, "old.png") && !strcmp(merged[3].id, "srv-dup") && merged[3].width == 10);
	assert(!strncmp(merged[1].id, LOCAL_ID_PREFIX, 6) && strlen(merged[1].id) == 14 && merged[1].width == 0);
	free(merged);
	merged = merge_local_screenshots(NULL, 0, &count);
	assert(count == 3);
	free(merged);
}

static void test_sha1(void) {
	char hex[41];
	write_file("/tmp/immich-sha1-test", "abc");
	assert(!hash_file("/tmp/immich-sha1-test", hex) && !strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d"));
	write_file("/tmp/immich-sha1-test", "");
	assert(!hash_file("/tmp/immich-sha1-test", hex) && !strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
	assert(hash_file("/tmp/immich-no-such-file", hex) == -1);
}

static void test_sync_index_round_trip_and_album_change(void) {
	use_fresh_userdata();
	strcpy(prefs.album_id, "album-one");
	SyncEntry entries[2] = {
		{"/s/a.png", 10, 1000, "aaaa", "asset-a"},
		{"/s/b.png", 20, 2000, "bbbb", ""},
	};
	index_save(&(SyncIndex){entries, 2});

	SyncIndex loaded;
	index_load(&loaded);
	assert(loaded.count == 2 && !strcmp(loaded.entries[0].path, "/s/a.png") && loaded.entries[0].size == 10);
	assert(loaded.entries[0].mtime == 1000 && !strcmp(loaded.entries[0].sha1, "aaaa") && !strcmp(loaded.entries[0].asset_id, "asset-a"));
	assert(!strcmp(loaded.entries[1].sha1, "bbbb") && !loaded.entries[1].asset_id[0]);
	free(loaded.entries);

	strcpy(prefs.album_id, "album-two");
	index_load(&loaded);
	assert(loaded.count == 2 && !strcmp(loaded.entries[0].sha1, "aaaa") && !loaded.entries[0].asset_id[0]);
	free(loaded.entries);
}

static void test_scan_detects_changes(void) {
	char dir[] = "/tmp/immich-scan-XXXXXX";
	assert(mkdtemp(dir));
	char same[300], resized[300], touched[300], fresh[300], notes[300];
	snprintf(same, sizeof(same), "%s/same.png", dir);
	snprintf(resized, sizeof(resized), "%s/resized.PNG", dir);
	snprintf(touched, sizeof(touched), "%s/touched.jpg", dir);
	snprintf(fresh, sizeof(fresh), "%s/fresh.jpeg", dir);
	snprintf(notes, sizeof(notes), "%s/notes.txt", dir);
	write_file(same, "1234");
	write_file(resized, "12345");
	write_file(touched, "123");
	write_file(fresh, "1");
	write_file(notes, "ignored");
	set_mtime(same, 5000);
	set_mtime(resized, 5000);
	set_mtime(touched, 5000);

	SyncEntry old_entries[3] = {{.size = 4, .mtime = 5000, .sha1 = "h1", .asset_id = "id1"},
		{.size = 4, .mtime = 5000, .sha1 = "h2", .asset_id = "id2"}, {.size = 3, .mtime = 4999, .sha1 = "h3", .asset_id = "id3"}};
	strcpy(old_entries[0].path, same);
	strcpy(old_entries[1].path, resized);
	strcpy(old_entries[2].path, touched);
	SyncIndex scanned;
	scan_dir(dir, &(SyncIndex){old_entries, 3}, &scanned);
	assert(scanned.count == 4);
	for (int i = 0; i < scanned.count; i++) {
		int kept = !strcmp(scanned.entries[i].path, same);
		assert(!strcmp(scanned.entries[i].asset_id, kept ? "id1" : "") && !strcmp(scanned.entries[i].sha1, kept ? "h1" : ""));
	}
	free(scanned.entries);
}

static int count_in_file(const char* path, const char* needle) {
	char* contents = read_file(path);
	int count = 0;
	for (char* at = contents ? strstr(contents, needle) : NULL; at; at = strstr(at + 1, needle)) count++;
	free(contents);
	return count;
}

static void make_screenshot(int number, const char* extension) {
	char path[300], text[64];
	snprintf(path, sizeof(path), SDCARD_ROOT "/Screenshots/shot%03d.%s", number, extension);
	snprintf(text, sizeof(text), "screenshot-%d", number);
	write_file(path, text);
}

static void test_sync_run_uploads_and_adds_duplicates(void) {
	use_fresh_userdata();
	setenv("PATH", "tests/fake_bin:/usr/bin:/bin", 1);
	setenv("FAKE_CURL_LOG", "/tmp/immich-fake-curl.log", 1);
	setenv("FAKE_CURL_ALL", "/tmp/immich-fake-curl-all.log", 1);
	strcpy(prefs.server_url, "https://photos.example.com");
	strcpy(prefs.api_key, "secret-key-123");

	SyncStatus progress;
	assert(sync_run() == -1);
	sync_status(&progress);
	assert(progress.state == SYNC_FAILED && strstr(progress.error, "Set up"));

	strcpy(prefs.album_id, "11111111-1111-4111-8111-111111111111");
	mkdir_p(SDCARD_ROOT "/Screenshots");
	system("rm -f " SDCARD_ROOT "/Screenshots/*");
	make_screenshot(1, "png");
	make_screenshot(2, "jpg");
	make_screenshot(3, "png");
	write_file(SDCARD_ROOT "/Screenshots/readme.txt", "not a screenshot");
	char duplicate_sha1[41];
	assert(!hash_file(SDCARD_ROOT "/Screenshots/shot002.jpg", duplicate_sha1));
	setenv("FAKE_DUPLICATE_SHA1", duplicate_sha1, 1);

	unlink("/tmp/immich-fake-curl-all.log");
	assert(sync_run() == 0);
	sync_status(&progress);
	assert(progress.state == SYNC_DONE && progress.total == 3 && progress.done == 3);
	assert(progress.uploaded == 2 && progress.existing == 1 && progress.failed == 0 && progress.bytes_done > 0);
	assert(count_in_file("/tmp/immich-fake-curl-all.log", "/api/assets\"") == 2);
	assert(count_in_file("/tmp/immich-fake-curl-all.log", "/api/albums/11111111-1111-4111-8111-111111111111/assets\"") == 1);
	assert(count_in_file("/tmp/immich-fake-curl-all.log", "dddddddd-dddd-4ddd-8ddd-dddddddddddd") == 1);
	assert(count_in_file("/tmp/immich-fake-curl-all.log", "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee") == 2);

	unlink("/tmp/immich-fake-curl-all.log");
	assert(sync_run() == 0);
	sync_status(&progress);
	assert(progress.state == SYNC_DONE && progress.total == 0);
	assert(access("/tmp/immich-fake-curl-all.log", F_OK));

	write_file(SDCARD_ROOT "/Screenshots/shot001.png", "edited screenshot");
	assert(sync_run() == 0);
	sync_status(&progress);
	assert(progress.total == 1 && progress.uploaded == 1);
	unsetenv("FAKE_DUPLICATE_SHA1");
}

static void test_sync_batches_and_failures(void) {
	use_fresh_userdata();
	strcpy(prefs.server_url, "https://photos.example.com");
	strcpy(prefs.api_key, "secret-key-123");
	strcpy(prefs.album_id, "11111111-1111-4111-8111-111111111111");
	setenv("FAKE_DUPLICATE_SHA1", "no-such-hash", 1);
	system("rm -f " SDCARD_ROOT "/Screenshots/*");
	for (int i = 0; i < 105; i++) make_screenshot(i, "png");

	unlink("/tmp/immich-fake-curl-all.log");
	assert(sync_run() == 0);
	SyncStatus progress;
	sync_status(&progress);
	assert(progress.uploaded == 105 && progress.failed == 0);
	assert(count_in_file("/tmp/immich-fake-curl-all.log", "/bulk-upload-check\"") == 3);
	assert(count_in_file("/tmp/immich-fake-curl-all.log", "/api/albums/11111111-1111-4111-8111-111111111111/assets\"") == 3);

	use_fresh_userdata();
	strcpy(prefs.server_url, "https://photos.example.com");
	strcpy(prefs.api_key, "secret-key-123");
	strcpy(prefs.album_id, "11111111-1111-4111-8111-111111111111");
	setenv("FAKE_CURL_FAIL", "1", 1);
	assert(sync_run() == 0);
	sync_status(&progress);
	assert(progress.state == SYNC_DONE && progress.failed == 105 && progress.uploaded == 0 && strstr(progress.error, "401"));
	SyncIndex index;
	index_load(&index);
	for (int i = 0; i < index.count; i++) assert(!index.entries[i].asset_id[0]);
	free(index.entries);
	unsetenv("FAKE_CURL_FAIL");

	assert(sync_run() == 0);
	sync_status(&progress);
	assert(progress.uploaded == 105);
	unsetenv("FAKE_DUPLICATE_SHA1");
}

static void test_stat_state_and_sync_lock(void) {
	assert(stat_state("812 (keymon.elf) T 1 812 812 0") == 'T');
	assert(stat_state("9 (odd) name) S 1") == 'S');

	use_fresh_userdata();
	strcpy(prefs.album_id, "11111111-1111-4111-8111-111111111111");
	char lock_path[320];
	prefs_file(lock_path, sizeof(lock_path), "sync.lock");
	int other_process_fd = open(lock_path, O_WRONLY | O_CREAT, 0644);
	assert(!flock(other_process_fd, LOCK_EX | LOCK_NB));
	SyncStatus progress;
	assert(sync_run() == 0);
	sync_status(&progress);
	assert(progress.state == SYNC_IDLE);
	close(other_process_fd);
}

static void test_daemon_progress(void) {
	use_fresh_userdata();
	int done = 0, total = 0;
	assert(!daemon_progress(&done, &total));
	write_status(1, 3, 12);
	assert(daemon_progress(&done, &total) && done == 3 && total == 12);
	write_status(0, 0, 0);
	assert(!daemon_progress(&done, &total));
}

int main(void) {
	test_prefs_round_trip();
	test_immich_parse_albums();
	test_parse_search_pages();
	test_parse_bulk_check();
	test_json_string_field();
	test_client_with_fake_curl();
	test_login();
	test_cache_evicts_previews_by_mtime_before_thumbs();
	test_list_json_round_trip();
	test_cache_lists_and_offline_albums();
	test_full_list_cache_and_slices();
	test_original_destination_and_detection();
	test_merge_local_screenshots();
	test_sha1();
	test_sync_index_round_trip_and_album_change();
	test_scan_detects_changes();
	test_sync_run_uploads_and_adds_duplicates();
	test_sync_batches_and_failures();
	test_stat_state_and_sync_lock();
	test_daemon_progress();
	puts("all tests passed");
	return 0;
}
