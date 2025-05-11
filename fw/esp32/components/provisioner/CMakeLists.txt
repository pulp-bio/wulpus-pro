idf_component_register(SRCS "provisioner.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_event wifi_provisioning)
