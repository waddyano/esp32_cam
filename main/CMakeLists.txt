set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


idf_component_register(SRCS "camera_main.cpp" "wifi.cpp" "camera.cpp" "httpd_util.cpp" "index.cpp" "ota.cpp" "sse.cpp"
                    INCLUDE_DIRS "")
