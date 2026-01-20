# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.
idf_component_register( SRCS 
                            "main.c"
                            "user/src/cap_touch.c"
                            "user/src/led.c"
                            "user/src/line_data.c"
                            "user/src/line_state.c"
                            "user/src/tripring.c"
                            "user/src/http_client.c"
                            "user/src/requests.c"
                        INCLUDE_DIRS 
                            "."
                            "user/inc"
                        REQUIRES 
                            "provisioning"
                            "sntp_time_server"
                        )
spiffs_create_partition_image(spiffs data)
