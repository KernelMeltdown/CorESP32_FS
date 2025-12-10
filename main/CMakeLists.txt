# Main Component für CoreFS

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_partition
        # Weitere Komponenten hier hinzufügen wenn benötigt:
        # driver  # Für GPIO, SPI, etc.
        # nvs_flash
        # corefs  # Wenn CoreFS Komponente hinzugefügt
)