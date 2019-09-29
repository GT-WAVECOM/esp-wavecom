deps_config := \
	/home/robert/esp/esp-adf/esp-idf/components/app_trace/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/aws_iot/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/bt/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/driver/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/esp32/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/esp_adc_cal/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/esp_event/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/esp_http_client/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/esp_http_server/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/ethernet/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/fatfs/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/freemodbus/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/freertos/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/heap/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/libsodium/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/log/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/lwip/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/mbedtls/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/mdns/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/mqtt/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/nvs_flash/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/openssl/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/pthread/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/spi_flash/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/spiffs/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/tcpip_adapter/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/vfs/Kconfig \
	/home/robert/esp/esp-adf/esp-idf/components/wear_levelling/Kconfig \
	/home/robert/esp/esp-adf/components/audio_board/Kconfig.projbuild \
	/home/robert/esp/esp-adf/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/robert/esp/esp-adf/components/esp-adf-libs/Kconfig.projbuild \
	/home/robert/esp/esp-adf/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/robert/esp/voip/main/Kconfig.projbuild \
	/home/robert/esp/esp-adf/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/robert/esp/esp-adf/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)

ifneq "$(IDF_CMAKE)" "n"
include/config/auto.conf: FORCE
endif

$(deps_config): ;
