idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES ulp nvs_flash)

set(ulp_app_name lp_core_${COMPONENT_NAME})
set(ulp_lp_core_sources "lp_core/main.c")
set(ulp_exp_dep_srcs "main.c")
ulp_embed_binary(${ulp_app_name} "${ulp_lp_core_sources}" "${ulp_exp_dep_srcs}")