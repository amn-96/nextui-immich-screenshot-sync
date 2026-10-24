# Configuration Guide

This pak can be configured through its own UI as well as via its preferences file at `.userdata/shared/Immich/prefs.txt`. You can access this via NextUI's HTTP Filebrowser pak, SSH, ADB, or by plugging the SD card into another computer.

## Connecting to Immich

The app talks to Immich via an API key. There are two ways to give it one.

### Option 1: sign in on the device (default)

1. Open **Tools > Immich Sync > Settings > Immich Settings**.
2. Enter your **Server URL** (e.g. `https://photos.example.com`), **Email** and **Password**.
3. Choose **Sign In**.

The app logs in once (`POST /api/auth/login`) and uses that session to create an API key named `NextUI <hostname>` (`POST /api/api-keys`). Only the server URL, email and key are saved. The password is never written to disk, and the session token is discarded.

The key it creates is limited to what the app needs. None of these permissions can delete anything:

| Permission | Used for |
|---|---|
| `asset.read` | Listing screenshots and storage size |
| `asset.view` | Thumbnails |
| `asset.download` | Full-size images in the viewer |
| `asset.upload` | Uploading screenshots |
| `album.read` | Album list and screenshot counts |
| `album.create` | Creating this device's album |
| `albumAsset.create` | Adding uploads to the album |
| `user.read` | Showing your Immich user name |

You can see or revoke the key in the Immich web UI under **Account Settings > API Keys**.

### Option 2: BYO API Key

Custom API keys are fully supported and how I connect. I prefer this because my Immich server has password login disabled (OAuth-only). Immich rejects password sign-in from every client but API Keys still work. If you use this method, you currently have to write straight to the configuration file, which I would do anyway since the api key is 40 characters long...

1. In Immich, go to **Account Settings > API Keys > New API Key**. Grant at least the permissions in the table above. Granting "all" also works, but it gives the device more access than it needs. There is **no delete call to Immich in this pak**, but I always recommend scoping permissions.
2. Edit `.userdata/shared/Immich/prefs.txt`. If the file doesn't exist, create it.

       server_url=https://photos.example.com
       api_key=<your key>

3. Open/restart the app. **Settings > Immich Settings** should now show your server version and user name.

Choosing **Sign In** later replaces your custom key with a newly created one.

## Settings

All under **Tools > Immich Sync > Settings**. The `prefs.txt` key is listed where there is one.

### Immich Settings

Server URL (`server_url`), Email (`user_email`), Password and **Sign In** (see above). Once connected, it also shows your server version, Immich user, this device's screenshot count, and the count and storage size of all matched screenshots.

### Sync Settings

| Setting | What it does |
|---|---|
| **Force Sync** | Upload new screenshots now. |
| **Reset Sync Settings** | Clears this device's album and match key so you can set them up again. Nothing is deleted on Immich. |
| **Daemon Autostart** (`daemon_autostart`) | Starts a background daemon at boot that uploads new screenshots as they appear. Pauses during sleep. |
| **Background Sync** (`background_sync`) | Lets the daemon keep uploading while a game runs. Off pauses it until you exit the game. |
| **Synced Album**, **Album Match Key** | Read-only. Set during first-run setup (`album_label`, `match_key`, `album_id`). |

#### How "All Screenshots" Works

The synced album you created during onboarding is named `<album name><match key>`, e.g. `Brick_NextUI`. The contents of any album on your Immich server **ending in the same match key**(case-sensitive) will automatically show up in **All Screenshots**. I do something like `Brick_Screenshots`, `Odin_Screenshots`, `PC_Screenshots`.

### Cache

Thumbnails and previews are cached on the SD card. 

| Setting | What it does |
|---|---|
| **Used** | Cache space in use. |
| **Cache Limit** (`cache_limit_mb`, `0` = unlimited) | 500MB to 10GB, or Unlimited. Default 2GB. Oldest previews are evicted first. |
| **Rebuild All Thumbnails** | Deletes cached thumbnails; they download again as you browse. |
| **Clear Cache** | Deletes all cached Immich data. Your screenshots and the server are untouched. |

### Screenshot Albums

Pick extra albums (`extra_album_ids`) to include in **All Screenshots** alongside the match-key albums.

### Grid Size

**Standard** (4x3), **Dense** (8x6) or **Custom** (`grid_mode` 0/1/2). Custom uses **Horizontal** and **Vertical** (`grid_custom_cols`, `grid_custom_rows`, 1 to 12).
