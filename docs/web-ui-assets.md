# Web UI static asset layout

The rotator web UI is served from standalone static assets instead of being embedded inside `src/web_server.cpp`.

Files:

- `web/index.html`
- `web/app.css`
- `web/app.js`

The C++ web server still owns the HTTP listener and API proxy routes. Static assets are loaded from `/opt/pi-satellite-rotator/web` on an installed Pi, with local development fallbacks for `web/` and `../web/`.

The installer copies the assets to `/opt/pi-satellite-rotator/web`. Keep the service in `--motor-backend simulator` until the rotator is mechanically assembled and limit-switch zeroing has been validated.

Docker native tests also use the `/src/web` fallback because CTest runs from `/build` while the source tree is copied to `/src` inside the test image.
