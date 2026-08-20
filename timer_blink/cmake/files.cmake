# Add sources to executable/library
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/syscall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/sysmem.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Src/startup_stm32g474xx.S"

    # CMSIS device
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32G4xx/Source/Templates/system_stm32g4xx.c"

    # STM32 HAL driver (minimal set: RCC/GPIO/DMA/EXTI/FLASH/PWR/CORTEX/TIM)
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma_ex.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_exti.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_tim_ex.c"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc/Legacy"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    STM32G474xx
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/stm32g474xe_flash.ld" "${CMAKE_CURRENT_BINARY_DIR}" COPYONLY)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/stm32g474xe_flash.ld")
