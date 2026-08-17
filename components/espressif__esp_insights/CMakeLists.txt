# Stands in for espressif/esp_insights, which the Arduino component lists as an
# optional dependency. The published package omits the certificate its own
# CMakeLists embeds, so it cannot build; nothing here uses Espressif's cloud
# services. Wired in through src/idf_component.yml, which is the documented
# way to replace a managed component with a local one.
idf_component_register()
