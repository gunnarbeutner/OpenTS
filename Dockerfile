# syntax=docker/dockerfile:1

# The browser target, built and then served as static files. The game data is not part of
# this image and never should be: an OpenTS-Assets "build/web" tree is mounted in at run
# time by whoever has one.

# The tag pins the Emscripten the tree is developed against. The build is the one
# docs/BUILDING.md describes for a page, so NODERAWFS is off.
#
# The engine is built twice, because how a wait hands the thread back is a link-time
# decision and no one module runs everywhere: the JSPI build is the one to run and the
# Asyncify build is what a browser without JSPI falls back to. Both land in one directory
# and the page loads whichever the browser can run.
FROM emscripten/emsdk:6.0.8 AS build

RUN apt-get update \
 && apt-get install --no-install-recommends --yes ninja-build \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Both engines are built in one step, into cache mounts that outlive the image. Any change
# to the tree invalidates the COPY above and so re-runs this, but ninja then finds almost
# everything already made: COPY carries the build context's own timestamps, so a file that
# did not change does not look changed. What that saves is not only the compiles -- at -O2
# the link runs binaryen over the module, and instruments a far larger one for Asyncify.
#
# "--build-arg OPENTS_CLEAN_BUILD=1" throws both away first. A release is built that way,
# because an incremental build is only as trustworthy as the cache it stood on.
#
# A cache mount is not part of this stage, so nothing later can COPY out of one. The
# artifacts are taken out here, into ordinary directories that every step below reads.
ARG OPENTS_CLEAN_BUILD=0

RUN --mount=type=cache,id=opents-build,target=/src/build \
    --mount=type=cache,id=opents-build-asyncify,target=/src/build-asyncify \
    if [ "$OPENTS_CLEAN_BUILD" = "1" ]; then rm -rf build/* build-asyncify/*; fi \
 && emcmake cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPENTS_WASM_NODERAWFS=OFF \
        -DOPENTS_MOVIE_FORMAT=MP4 \
        -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" \
        -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=FS,callMain,UTF8ToString -O2" \
 && ninja -C build OpenTS \
 && emcmake cmake -S . -B build-asyncify -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPENTS_WASM_NODERAWFS=OFF \
        -DOPENTS_WASM_SUSPEND=ASYNCIFY \
        -DOPENTS_MOVIE_FORMAT=MP4 \
        -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" \
        -DCMAKE_EXE_LINKER_FLAGS="-sEXPORTED_RUNTIME_METHODS=FS,callMain,UTF8ToString -O2" \
 && ninja -C build-asyncify OpenTS \
 && rm -rf /src/stage /src/stage-asyncify \
 && mkdir -p /src/stage /src/stage-asyncify \
 && cp -a build/bin/. /src/stage/ \
 && cp -a build-asyncify/bin/. /src/stage-asyncify/ \
 && stale=$(find code wasm -type f -newer /src/stage/Game.wasm -print -quit) \
 && if [ -n "$stale" ]; then \
        echo "the cached build is stale: $stale is newer than the module it produced"; \
        exit 1; \
    fi \
 && echo "staged $(ls /src/stage | wc -l) files, none older than their sources"

# The page's own three files are minified here, where node already lives. The engine
# modules are left exactly as emcc wrote them: Game.js carries the JSPI and Asyncify glue
# emscripten generated and already minified, and a second pass over it risks breaking the
# instrumentation rather than saving anything worth having.
#
# Quotes are kept as written, because the serve stage patches the hashed module names into
# page.js and the hashed page names into index.html by matching the double-quoted logical
# name. Attribute quotes stay for the same reason.
RUN npm install --no-fund --no-audit --loglevel=error -g \
        terser@5.44.0 clean-css-cli@5.6.3 html-minifier-terser@7.2.0 \
 && terser /src/stage/page.js --compress --mangle --format quote_style=3 \
        --output /src/stage/page.js \
 && cleancss --output /src/stage/page.css /src/stage/page.css \
 && html-minifier-terser --collapse-whitespace --remove-comments \
        --output /src/stage/index.html /src/stage/index.html \
 && node --check /src/stage/page.js

# Sign the engine-module update tree the Tauri shell's updater consumes
# (tauri/src-tauri/src/updater.rs). This is a separate representation from the
# browser-direct hashed serving in the serve stage: the shell installs these five
# files under its own origin and loads them by logical name, so the signed index.html
# is the raw build output that still references "Game.js" and "Game-asyncify.js", not
# the browser-direct hashed one. The Ed25519 private key is mounted for this step only
# through a BuildKit secret; it never enters a build argument, environment variable, or
# image layer. Without the secret the tree is left empty and the shell falls back to
# its bundled copy. Build with:
#   DOCKER_BUILDKIT=1 docker build --secret id=engine_signing_key,src=.sigkey ...
RUN --mount=type=secret,id=engine_signing_key,required=false \
    mkdir -p /src/engine-tree \
 && if [ -s /run/secrets/engine_signing_key ]; then \
        "${EMSDK_NODE:-node}" tools/sign-engine/sign-engine.mjs sign \
            --index /src/stage/index.html \
            --page-css /src/stage/page.css \
            --page-js /src/stage/page.js \
            --game-js /src/stage/Game.js \
            --game-wasm /src/stage/Game.wasm \
            --asyncify-js /src/stage-asyncify/Game-asyncify.js \
            --asyncify-wasm /src/stage-asyncify/Game-asyncify.wasm \
            --key /run/secrets/engine_signing_key \
            --out /src/engine-tree \
            --sequence "$(date -u +%Y%m%d%H%M%S)"; \
    else \
        echo "note: no engine_signing_key secret; shell updater tree not published"; \
    fi


# Nothing here needs to be a program: the page reads its archives with HTTP range requests, so
# a static server that answers them is the whole requirement.
FROM nginx:alpine AS serve

COPY --from=build /src/stage/index.html /src/stage/robots.txt /src/stage/favicon.ico /src/stage/apple-touch-icon.png /src/stage/Game.js /src/stage/Game.wasm /usr/share/nginx/html/
COPY --from=build /src/stage/manifest.webmanifest /src/stage/sw.js /src/stage/icon-192.png /src/stage/icon-512.png /usr/share/nginx/html/
COPY --from=build /src/stage/page.css /src/stage/page.js /usr/share/nginx/html/
COPY --from=build /src/stage-asyncify/Game-asyncify.js /src/stage-asyncify/Game-asyncify.wasm /usr/share/nginx/html/

# The signed shell-updater tree (engine.json pointer, engine/<hash>.json descriptor,
# engine/files/<name>.<hash>.<ext>). Empty when the build ran without the signing
# secret. The browser-direct hashing below never touches the engine/ subtree.
COPY --from=build /src/engine-tree/ /usr/share/nginx/html/

# An OpenTS-Assets "build/web" tree, mounted read only at /usr/share/nginx/assets by
# compose.yaml's OPENTS_ASSETS. code/manifest.cpp fetches "assets.json" and whatever
# manifest and files it names relative to the page (Module.opentsManifestBase is unset by
# default), so that tree's own layout -- assets.json, assets/<hash>.json,
# files/<name>.<hash>.<ext> -- is exposed unchanged at the site root rather than translated
# into anything else. Nginx serves byte ranges out of a static file by default, which is
# what the engine's archive reads need; nothing here disables that.
#
# A shared snippet rather than one written into a single server{} block: a plain
# "docker compose up" is reached at the literal host "localhost", which nginx's own
# default.conf still claims by exact server_name match ahead of the default_server
# catch-all below, so both server blocks include this one file rather than risk the two
# drifting apart.
#
# assets.json is the one mutable pointer OpenTS-Assets publishes -- overwritten in place
# each release. Everything it leads to, the hashed manifest and the files it names, is
# named for its own content and so is immutable forever; a CDN in front of this image
# (Cloudflare included) is free to hold either at the edge indefinitely.
#
# The mutable documents carry "max-age=0, s-maxage=86400" rather than "no-cache". A browser
# still revalidates every one of them, which is what a pointer has to do; the difference is
# that a shared cache answers that from the edge instead of asking here. Under "no-cache"
# every visitor cost this origin one round trip per pointer, and relay.json is generated
# rather than served from a file, so it has no validator and could not even answer 304.
#
# A day of edge lifetime is only safe because publishing ends by purging these paths: the
# release is seen at once because the old copy is dropped, not because it expired. A deploy
# that cannot purge leaves the previous release served for up to a day with no way to
# correct it, which is why deploy.py treats a failed purge as a failed deploy.
#
# Nothing here is ever offered pre-compressed. A cache in front of this image (Cloudflare
# included) can fill itself for a byte-range request with a plain GET of its own -- Range
# header dropped, its own Accept-Encoding attached -- and a compressed reply to that fetch
# gets cached as if it were the whole object; every client Range request the cache answers
# afterward is then sliced against that wrong, compressed length instead of the true size.
# Nothing here can tell that fetch apart from an ordinary one, so a smaller transfer was not
# worth a silently corrupted one.
RUN mkdir -p /etc/nginx/snippets && cat <<'EOF' > /etc/nginx/snippets/opents-assets.conf
location = /assets.json {
    alias /usr/share/nginx/assets/assets.json;
    default_type application/json;
    add_header Cache-Control "public, max-age=0, s-maxage=86400" always;
    include /etc/nginx/snippets/opents-assets-cors.conf;
}

# Everything a release publishes lives under here, so the fallback type is the one for
# bytes rather than for the manifests: a .MIX has no entry in mime.types and would
# otherwise be answered as whatever this names. The manifests and profiles are .json and
# nginx types those from the extension without being told.
location /assets/ {
    alias /usr/share/nginx/assets/assets/;
    default_type application/octet-stream;
    add_header Cache-Control "public, max-age=31536000, immutable" always;
    include /etc/nginx/snippets/opents-assets-cors.conf;
}

location /files/ {
    alias /usr/share/nginx/assets/files/;
    add_header Cache-Control "public, max-age=31536000, immutable" always;
    include /etc/nginx/snippets/opents-assets-cors.conf;
}

# The native installers the page offers, mounted read only beside the asset tree.
# downloads.json is the one mutable file -- rewritten whenever a build is published --
# and every installer under it is named for its own hash, so it is cached forever.
# A deployment with no installers answers 404 here and the page offers nothing.
location = /downloads.json {
    alias /usr/share/nginx/downloads/downloads.json;
    default_type application/json;
    add_header Cache-Control "public, max-age=0, s-maxage=86400" always;
    include /etc/nginx/snippets/opents-assets-cors.conf;
}

location ^~ /downloads/ {
    alias /usr/share/nginx/downloads/downloads/;
    default_type application/octet-stream;
    add_header Cache-Control "public, max-age=31536000, immutable" always;
    include /etc/nginx/snippets/opents-assets-cors.conf;
}

# The signed engine-module tree the Tauri shell's updater reads, served from the html
# root next to the browser page. engine.json is the one mutable pointer -- overwritten
# each release -- so it is never cached, like assets.json above. Everything it names,
# the hashed descriptor and the content-addressed files under engine/, is immutable
# forever. "^~" so these outrank the ".js"/".wasm" regex locations in opents-page.conf,
# which would otherwise claim engine/files/*.js and *.wasm by suffix.
# A worker is only replaced by one the browser fetched, so a cached copy of this file is a
# deployment that can never be corrected. The manifest is small and names hashed icons.
location = /sw.js {
    root /usr/share/nginx/html;
    default_type application/javascript;
    add_header Cache-Control "public, max-age=0, s-maxage=86400" always;
    add_header Service-Worker-Allowed "/" always;
}

location = /manifest.webmanifest {
    root /usr/share/nginx/html;
    default_type application/manifest+json;
    add_header Cache-Control "public, max-age=0, s-maxage=86400" always;
}

location = /engine.json {
    root /usr/share/nginx/html;
    default_type application/json;
    add_header Cache-Control "public, max-age=0, s-maxage=86400" always;
    include /etc/nginx/snippets/opents-assets-cors.conf;
}

location ^~ /engine/ {
    root /usr/share/nginx/html;
    add_header Cache-Control "public, max-age=31536000, immutable" always;
    include /etc/nginx/snippets/opents-assets-cors.conf;
}

# Where the relay answers, which code/wsrelay.cpp reads before it opens a network game.
# The value comes from OPENTS_RELAY_URL through the map relay.conf.template writes, so it
# is a property of the deployment rather than of the image or the page.
location = /relay.json {
    default_type application/json;
    add_header Cache-Control "public, max-age=0, s-maxage=86400" always;
    return 200 '{"url":"$opents_relay_url"}';
}

EOF

# A native shell bundles the engine's own page locally and fetches this release over the
# network, so that page's origin is never this one -- an ordinary cross-origin read, and
# Range is not a CORS-safelisted header, so a ranged GET preflights with OPTIONS first. The
# release carries nothing a visitor did not already ask for by opening the page, so every
# origin answers alike; the manifest itself, and the sha256 every object is named for, are
# what a native shell already checks the bytes against, not this header.
#
# Max-Age is answered on every request rather than only inside the "if" below on purpose:
# nginx does not inherit a location's add_header directives into a nested block that adds
# one of its own, so an add_header kept inside the "if" would silently drop every header
# set above it -- the preflight would answer 204 with no Access-Control-Allow-* at all.
RUN cat <<'EOF' > /etc/nginx/snippets/opents-assets-cors.conf
add_header Access-Control-Allow-Origin '*' always;
add_header Access-Control-Allow-Methods 'GET, HEAD, OPTIONS' always;
add_header Access-Control-Allow-Headers 'Range' always;
add_header Access-Control-Expose-Headers 'Content-Range, Accept-Ranges, Content-Length, ETag' always;
add_header Access-Control-Max-Age 86400 always;

if ($request_method = OPTIONS) {
    return 204;
}
EOF
RUN sed -i '/server_name  localhost;/a\    include /etc/nginx/snippets/opents-assets.conf;' /etc/nginx/conf.d/default.conf

# A location matched by a compressed sibling's own suffix (".html.br", say) types the
# response off that suffix, not off the ".html" in front of it -- nginx has no notion of a
# compound extension -- so the page arrived at the browser as application/octet-stream and
# was offered as a download instead of rendered. default_type does not take a variable, and
# add_header cannot override the type nginx already set for a served file, so each build
# output type gets its own regex location instead, naming the one correct type outright. A
# regex location outranks location / for the request it matches regardless of which is
# declared first, including one index's own internal redirect from "/" reaches by name.
RUN cat <<'EOF' > /etc/nginx/snippets/opents-page.conf
location ~ \.html$ {
    root /usr/share/nginx/html;
    default_type text/html;
    add_header Cache-Control $asset_cache_control always;
}

location ~ \.js$ {
    root /usr/share/nginx/html;
    default_type application/javascript;
    add_header Cache-Control $asset_cache_control always;
}

location ~ \.wasm$ {
    root /usr/share/nginx/html;
    default_type application/wasm;
    add_header Cache-Control $asset_cache_control always;
}

# Apple's agents ask for the touch icon under a family of names -- a size in the middle,
# a "-precomposed" suffix, or neither -- and a link preview goes on asking whatever the
# page itself declares, because it never parses the page. One icon answers all of them
# rather than each spelling being a 404 in the log.
location ~ ^/apple-touch-icon(-[0-9]+x[0-9]+)?(-precomposed)?\.png$ {
    root /usr/share/nginx/html;
    try_files /apple-touch-icon.png =404;
}
EOF
RUN sed -i '/include \/etc\/nginx\/snippets\/opents-assets.conf;/a\    include /etc/nginx/snippets/opents-page.conf;' /etc/nginx/conf.d/default.conf

# A redirect carries only the path, so the browser resolves it against the origin it
# actually used. nginx would otherwise build an absolute one out of what it sees itself,
# which behind a TLS-terminating proxy is plain HTTP and the container's own address; it
# does not consult X-Forwarded-Proto or X-Forwarded-Host to correct that.
RUN printf 'absolute_redirect off;\n' > /etc/nginx/conf.d/proxy.conf

# A "www." a caller happened to type is one origin more than the page needs; folding it
# into the bare host keeps every reader on the same one rather than splitting storage and
# saved games across two. The base image's own server block is untouched and stays the
# default for every other host, this one only matching a "www." prefix and never listed
# first, so nothing here can become the default server by file order.
#
# $scheme is plain http behind the same TLS-terminating proxy proxy.conf answers for, so
# the redirect target comes from X-Forwarded-Proto where the proxy sends one, the same
# origin the request actually arrived on rather than the one nginx sees itself.
RUN cat <<'EOF' > /etc/nginx/conf.d/www.conf
map $http_x_forwarded_proto $www_redirect_scheme {
    ''      $scheme;
    default $http_x_forwarded_proto;
}

server {
    listen 80;
    server_name ~^www\.(?<bare_host>.+)$;
    return 301 $www_redirect_scheme://$bare_host$request_uri;
}
EOF

# The quick tunnel this deployment was first reachable through. Its hostname answers
# nothing but a note saying where the game went, so that a bookmark is told rather than
# served a second copy. An exact server_name outranks the default_server below, and the
# answer is given for every path rather than for "/" alone: half of a page loading from a
# retired hostname is worse than none of it.
#
# This block and the tunnel are retired together; neither outlives the other usefully.
RUN cat <<'EOF' > /etc/nginx/conf.d/retired.conf
server {
    listen 80;
    server_name reviewer-smoke-resumes-care.trycloudflare.com;

    default_type text/plain;
    add_header Cache-Control "public, max-age=0, s-maxage=86400" always;
    return 200 "use play-ts.net\n";
}
EOF

# The build outputs the page loads by name are renamed to carry a hash of their own
# content, so cache.conf below can tell every cache in front of this image to keep one
# forever: its name changes the moment its bytes do, and never otherwise. Each file is
# hashed only once everything it names has been patched into it, because a file's hash has
# to be the last thing computed from it: the .wasm files first, then the module loaders
# that name them, then page.js which names the loaders, then index.html which names
# page.js and page.css. index.html is not renamed: it is what names the current hashes, so
# it is the one response every load has to revalidate, and it is now markup alone.
#
# The icon is copied rather than renamed. The page loads the hashed name and keeps it
# forever, while an agent that never parses the page goes on asking for /favicon.ico by
# convention, and that spelling has to keep answering.
RUN cd /usr/share/nginx/html && \
    GAME_WASM_HASH=$(sha256sum Game.wasm | cut -c1-12) && \
    ASYNC_WASM_HASH=$(sha256sum Game-asyncify.wasm | cut -c1-12) && \
    mv Game.wasm "Game.$GAME_WASM_HASH.wasm" && \
    mv Game-asyncify.wasm "Game-asyncify.$ASYNC_WASM_HASH.wasm" && \
    sed -i "s/\"Game\.wasm\"/\"Game.$GAME_WASM_HASH.wasm\"/g; s/'Game\.wasm'/'Game.$GAME_WASM_HASH.wasm'/g" Game.js && \
    sed -i "s/\"Game-asyncify\.wasm\"/\"Game-asyncify.$ASYNC_WASM_HASH.wasm\"/g; s/'Game-asyncify\.wasm'/'Game-asyncify.$ASYNC_WASM_HASH.wasm'/g" Game-asyncify.js && \
    GAME_JS_HASH=$(sha256sum Game.js | cut -c1-12) && \
    ASYNC_JS_HASH=$(sha256sum Game-asyncify.js | cut -c1-12) && \
    mv Game.js "Game.$GAME_JS_HASH.js" && \
    mv Game-asyncify.js "Game-asyncify.$ASYNC_JS_HASH.js" && \
    sed -i "s/\"Game\.js\"/\"Game.$GAME_JS_HASH.js\"/g; s/'Game\.js'/'Game.$GAME_JS_HASH.js'/g" page.js && \
    sed -i "s/\"Game-asyncify\.js\"/\"Game-asyncify.$ASYNC_JS_HASH.js\"/g; s/'Game-asyncify\.js'/'Game-asyncify.$ASYNC_JS_HASH.js'/g" page.js && \
    PAGE_CSS_HASH=$(sha256sum page.css | cut -c1-12) && \
    mv page.css "page.$PAGE_CSS_HASH.css" && \
    sed -i "s/\"page\.css\"/\"page.$PAGE_CSS_HASH.css\"/g; s/'page\.css'/'page.$PAGE_CSS_HASH.css'/g" index.html && \
    PAGE_JS_HASH=$(sha256sum page.js | cut -c1-12) && \
    mv page.js "page.$PAGE_JS_HASH.js" && \
    sed -i "s/\"page\.js\"/\"page.$PAGE_JS_HASH.js\"/g; s/'page\.js'/'page.$PAGE_JS_HASH.js'/g" index.html && \
    FAVICON_HASH=$(sha256sum favicon.ico | cut -c1-12) && \
    cp favicon.ico "favicon.$FAVICON_HASH.ico" && \
    sed -i "s/\"favicon\.ico\"/\"favicon.$FAVICON_HASH.ico\"/" index.html && \
    for icon in icon-192 icon-512; do \
        ICON_HASH=$(sha256sum "$icon.png" | cut -c1-12) && \
        mv "$icon.png" "$icon.$ICON_HASH.png" && \
        sed -i "s/\"$icon\.png\"/\"$icon.$ICON_HASH.png\"/" manifest.webmanifest; \
    done && \
    TOUCH_HASH=$(sha256sum apple-touch-icon.png | cut -c1-12) && \
    cp apple-touch-icon.png "apple-touch-icon.$TOUCH_HASH.png" && \
    sed -i "s/\"apple-touch-icon\.png\"/\"apple-touch-icon.$TOUCH_HASH.png\"/" index.html

# Every module and page name the served files reference has to exist here. The patching
# above matches quoted logical names and emcc chooses its own quoting, so a change in what
# it emits would otherwise leave a logical name in place and 404 at run time. The patching
# spells both quotings out rather than capturing one: busybox sed does not resolve a
# backreference on the single long line the minifier leaves behind, and fails silently.
RUN cd /usr/share/nginx/html && \
    for name in $(grep -ohE "(Game(-asyncify)?|page)\.[0-9a-zA-Z.]*(js|wasm|css)" \
            index.html page.*.js Game.*.js Game-asyncify.*.js | sort -u); do \
        [ -f "$name" ] || { echo "referenced but not served: $name"; exit 1; }; \
    done && echo "every referenced module and page file is present"

# A hashed file is immutable by construction and safe to cache forever, at the browser and
# at any CDN in front of it; index.html is what currently names those hashes, so it is the
# one response every load has to revalidate rather than trust. $uri rather than a location
# block, since conf.d is included at the http level, outside every server{} this image
# defines.
# The relay answers on a host of its own, so the page is told where rather than deriving it.
# Read when the container starts, not when the image is built, and answered from a location
# rather than a file so that nothing has to be written into the served directory. Empty is a
# deployment with no relay, which the engine reads as one where no network game can start.
ENV OPENTS_RELAY_URL=

RUN mkdir -p /etc/nginx/templates && cat <<'EOF' > /etc/nginx/templates/relay.conf.template
map $host $opents_relay_url {
    default "${OPENTS_RELAY_URL}";
}
EOF

RUN cat <<'EOF' > /etc/nginx/conf.d/cache.conf
map $uri $asset_cache_control {
    default                                             "public, max-age=0, s-maxage=86400";
    "~*^/Game(-asyncify)?\.[0-9a-f]{12}\.(js|wasm)$"     "public, max-age=31536000, immutable";
    "~*^/page\.[0-9a-f]{12}\.(js|css)$"                  "public, max-age=31536000, immutable";
    "~*^/favicon\.[0-9a-f]{12}\.ico$"                    "public, max-age=31536000, immutable";
    "~*^/(apple-touch-icon|icon-\d+)\.[0-9a-f]{12}\.png$" "public, max-age=31536000, immutable";
}

add_header Cache-Control $asset_cache_control always;
EOF

# The catch-all every hostname that is not claimed by name lands on. It serves the same
# page from the same snippet as default.conf, so the two cannot drift.
#
# It does not redirect http to https: the TLS-terminating proxy in front of this image is
# what decides that, and an origin that redirects on its own can only get it wrong -- the
# dev container has no TLS in front of it at all and would have sent itself somewhere
# nothing is listening.
RUN cat <<'EOF' > /etc/nginx/conf.d/catch-all.conf
server {
    listen 80 default_server;
    listen [::]:80 default_server;
    server_name _;

    location / {
        root   /usr/share/nginx/html;
        index  index.html index.htm;
    }

    include /etc/nginx/snippets/opents-assets.conf;

    error_page 500 502 503 504 /50x.html;
    location = /50x.html {
        root /usr/share/nginx/html;
    }
}
EOF

EXPOSE 80
