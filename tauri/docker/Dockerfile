FROM rust:1.90-bookworm

# Tauri v2's Linux prerequisites, plus patchelf and libfuse2 for the AppImage bundler.
# Bookworm is the oldest Debian carrying webkit2gtk-4.1; the glibc it links against sets
# the floor for the systems the AppImage runs on.
RUN apt-get update && apt-get install -y --no-install-recommends \
		build-essential \
		ca-certificates \
		curl \
		desktop-file-utils \
		file \
		libayatana-appindicator3-dev \
		libfuse2 \
		librsvg2-dev \
		libssl-dev \
		libwebkit2gtk-4.1-dev \
		libxdo-dev \
		nodejs \
		npm \
		patchelf \
		pkg-config \
		squashfs-tools \
		wget \
		zsync \
	&& rm -rf /var/lib/apt/lists/*

# The npm CLI ships a prebuilt binary for the image's architecture, which the cargo one
# would spend the length of a second build compiling.
RUN npm install -g @tauri-apps/cli@2.11.4 && npm cache clean --force

COPY build-appimage.sh /usr/local/bin/build-appimage
RUN chmod +x /usr/local/bin/build-appimage

WORKDIR /work
ENTRYPOINT ["/usr/local/bin/build-appimage"]
