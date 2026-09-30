FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /src

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    g++ \
    git \
    ninja-build \
    python3-venv \
    qt6-base-dev \
    qt6-base-private-dev \
    libqt6sql6-sqlite \
    && rm -rf /var/lib/apt/lists/*

COPY . .
RUN python3 -m venv .tools/python \
    && .tools/python/bin/pip install --disable-pip-version-check pypdfium2==5.13.0 \
    && .tools/python/bin/python scripts/prepare-pdfium.py
RUN cmake -S . -B build/container -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build/container --target muzakere_server muz_pdf_worker muz_tests --parallel 4 \
    && QT_QPA_PLATFORM=offscreen build/container/muz_tests "[web]" \
    && QT_QPA_PLATFORM=offscreen ctest --test-dir build/container -R server-smoke --output-on-failure \
    && build/container/muzakere_server --workspace /tmp/muz-smoke --port 0 --smoke-test

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    fonts-dejavu-core \
    libqt6core6t64 \
    libqt6gui6 \
    qt6-qpa-plugins \
    libqt6network6 \
    libqt6sql6 \
    libqt6sql6-sqlite \
    libqt6widgets6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/container/muzakere_server /app/
COPY --from=build /src/build/container/muz_pdf_worker /app/
COPY --from=build /src/build/container/libpdfium.so /app/
EXPOSE 8080
ENV MUZ_WORKSPACE=/data/workspace
CMD ["/app/muzakere_server", "--host", "0.0.0.0", "--port", "8080"]
