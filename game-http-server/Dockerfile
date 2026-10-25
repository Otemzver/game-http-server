FROM praktikumcpp/practicum_cpp_backend:latest

WORKDIR /app

COPY conanfile.txt CMakeLists.txt ./

RUN mkdir build \
    && cd build \
    && conan install .. \
        --build=missing \
        -s build_type=Release \
        -s compiler=gcc \
        -s compiler.version=11 \
        -s compiler.libcxx=libstdc++11 \
        -s compiler.cppstd=17

COPY src ./src
COPY tests ./tests
COPY data ./data
COPY static ./static

RUN cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && cmake --build . --config Release --parallel \
    && cp bin/game_server /app/game_server

ENTRYPOINT ["/app/game_server"]
CMD ["--config-file", "/app/data/config.json", "--www-root", "/app/static"]