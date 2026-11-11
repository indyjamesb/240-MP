# MyAnimeList

240-MP can update your [MyAnimeList](https://myanimelist.net) list when you watch an episode or movie from Plex, Jellyfin, or Emby. The list update is the same moment those servers already treat the item as watched: playback has passed 90% of its duration on the timeline 240-MP posts. Nothing is sent before that, and a failure (offline, expired token, no mapping) is logged and skipped. Playback is never interrupted.

The matching rules follow [Plezy](https://github.com/edde746/plezy) (GPLv3, the same licence as 240-MP). This is a separate implementation of that behaviour, not a copy of Plezy's Dart. Show and movie ids come from [Fribb/anime-lists](https://github.com/Fribb/anime-lists). Split-cour seasons and episode offsets come from [Anime-Lists/anime-lists](https://github.com/Anime-Lists/anime-lists). Both are downloaded as the daily `anime-mappings.bin.gz` asset from [edde746/anime-mappings](https://github.com/edde746/anime-mappings), cached in the 240-MP data directory, and refreshed about once a day.

## Register a client

MyAnimeList does not ship a shared client id for other apps. Create one at [myanimelist.net/apiconfig](https://myanimelist.net/apiconfig):

1. Add an app. The API id you want is **OAuth2 / API v2**.
2. Copy the **Client ID**. Copy the **Client Secret** too if MAL shows one (registered apps do).
3. Set the **App Redirect URL** to the exact URL 240-MP displays on the MyAnimeList screen, for example `http://192.168.1.20:8740/callback`. It has to match character for character, including `http`, the port, and `/callback`.

Put the credentials in `config.json` in the data directory (`~/.local/share/240-MP` on Linux, including Raspberry Pi OS; `~/Library/Application Support/240-MP` on macOS):

```json
{
  "modules": {
    "com.240mp.mal": {
      "enabled": true,
      "client_id": "your client id",
      "client_secret": "your client secret",
      "oauth_port": 8740,
      "redirect_host": ""
    }
  }
}
```

`redirect_host` is optional. Leave it empty and 240-MP uses the Pi's LAN IPv4 address. Set it when that address is not the one you registered (a stable hostname such as `240mp.local`, or a DHCP reservation's address). `oauth_port` defaults to 8740.

There is no on-screen keyboard for the client id. A CRT remote cannot type it, and MAL's id is meant to be pasted once on a machine where you can edit the file. Enable the module in Settings after the file is saved, then open **MyAnimeList** from the main menu.

## Connect from the couch

The account screen shows the redirect URL to register, then **Connect**.

240-MP starts a small HTTP server on the Pi and shows a QR code about half the screen tall, plus the same URL in text (`http://<lan-ip>:8740/`). The phone only has to open that short address. The page sends the phone to MyAnimeList to approve the app. MyAnimeList then redirects back to the Pi with the authorization code, and the TV shows the account as linked.

The phone has to be on a network that can reach the Pi. Guest Wi-Fi with client isolation, or a phone on cellular, cannot open `192.168.x.x`. The code MAL returns is on the order of a thousand characters, so typing it with a remote is not a real fallback.

If the phone cannot reach the Pi, the fallback is a file. While the QR is on screen, copy the `code` value from the redirect URL into `mal_oauth_code.txt` in the data directory (one line, or the whole redirect URL). Press select on the remote. 240-MP reads the file, deletes it, and finishes the login. A URL you opened on a computer that *can* see the Pi does not need the file: the redirect hits the Pi directly.

Tokens are stored in `mal_auth.json` in the data directory, mode `0600`, separate from `config.json`. The access token is refreshed automatically (MAL access tokens expire; the refresh token is what keeps the link). Disconnect from the account screen or from the module's Disconnect setting. Disconnect deletes the token file.

Login gives up after 10 minutes. Start it again from the account screen.

### Why this redirect

MAL's API v2 is OAuth 2 with PKCE, and today it only accepts the `plain` challenge method. It does not offer a device-code / TV login. The redirect URL is fixed when you register the app, so it cannot follow a Pi whose DHCP address changed unless you edit the registration or set `redirect_host` to a name that does not change.

A local callback is the path that works with a remote and no keyboard: the QR stays small enough to scan at 240p because it only contains the short LAN URL. Plezy, the reference for the mapping and the list update, avoids this by sending the browser through its own relay. 240-MP does not do that. The Pi talks to MyAnimeList directly, and the only extra host it contacts is GitHub, for the public mapping file.

## What gets written

When a library episode or movie crosses the watched point, and the ids map to a MAL anime:

- `num_watched_episodes` is set to that episode's number inside the MAL entry (season 2 episode 3 of a split cour is episode 3 of the second MAL id, not "episode 15 of the show").
- The list status becomes `watching`, or `completed` when that number is the entry's last episode. A movie is one episode and is marked completed.
- Progress is never lowered. If your list is already further along, the update is skipped. A finished entry is not pulled back to `watching` by an earlier episode.

Only items with a MAL id are sent. Plex GUIDs (`tvdb://`, `tmdb://`, `imdb://`, `anidb://`, and the legacy HAMA / agent guids) and Jellyfin/Emby `ProviderIds` are read from the **show** for an episode, plus the season and episode numbers. HAMA's plain `anidb` id is trusted for season 1, which is how that agent numbers a single entry. Later seasons use TVDB/TMDB instead.

## What does not scrobble

Scrobble is hooked to the library timeline those servers already receive:

- Plex `update_timeline` (the same call `trackProgress` turns off)
- Emby and Jellyfin playback progress and the final stopped report

These do not call that path, so they do not update MAL:

- Shuffle and jukebox playback, which report no timeline on purpose
- Plex live TV, which uses a separate tuner keep-alive
- A channels module that deliberately reports no timeline
- YouTube, local files, ambient mode, anything that is not a Plex/Jellyfin/Emby library item
- Trailers and other items that are not a movie or episode
- AniDB specials (season 0 in the episode map), which would otherwise rewind the main series

The module has to be enabled, and an account has to be linked. Turning the module off stops updates without deleting the token.

## Limitations

- One MAL account for the whole app, not one per Plex/Jellyfin user.
- The redirect URL is the Pi's LAN address unless you set `redirect_host`. If DHCP moves the Pi, update the MAL app registration or the setting.
- Out-of-order episodes set the counter to this episode when that is higher than the list, and otherwise leave it alone. 240-MP does not walk the whole season to fill gaps.
- Rewatches are not given a separate MAL rewatch flag. A play that would decrease the counter is skipped, which also leaves a completed show completed.
- Mapping misses (a show Fribb does not know, or a brand-new cour with no season row yet) are logged and skipped.
- The mapping file is a few megabytes unpacked and is parsed on the Pi when it is first needed. A failed download keeps the previous cache.
