# Reproducible build of totk-sync.nro.
#   docker build -t totk-sync-build switch/
#   docker run --rm -v "$PWD/switch:/src" totk-sync-build
# The .nro appears in switch/totk-sync.nro.
FROM devkitpro/devkita64:latest
RUN dkp-pacman -Syu --noconfirm --needed switch-dev switch-curl switch-mbedtls switch-zlib \
 && dkp-pacman -Scc --noconfirm
WORKDIR /src
CMD ["make", "-j4"]
