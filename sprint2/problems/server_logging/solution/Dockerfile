FROM gcc:11.3 AS build

RUN apt update && apt install -y python3-pip cmake && pip3 install conan==1.*

COPY conanfile.txt /app/
RUN mkdir /app/build && cd /app/build && conan install .. --build=missing -s compiler.libcxx=libstdc++11 -s build_type=Release

COPY CMakeLists.txt /app/
COPY src /app/src
RUN cd /app/build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -j2

FROM ubuntu:22.04 AS run
RUN groupadd -r www && useradd -r -g www www
USER www

COPY --from=build /app/build/bin/game_server /app/game_server
COPY data /app/data
COPY static /app/static

ENTRYPOINT ["/app/game_server", "/app/data/config.json", "/app/static/"]
