idf_component_register( SRCS 
                            "time_server.c"
                        INCLUDE_DIRS 
                            "."
                        REQUIRES 
                          "nvs_flash"
                          "esp_netif"
                        )
