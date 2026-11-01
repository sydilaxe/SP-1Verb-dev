# Makefile for Teenage Engineering SP-1 Stem Player Reverb Firmware
# Target: Nordic nRF52840 (ARM Cortex-M4F)
# Outputs: build/sp1_verb.bin, build/sp1_verb.hex

TARGET := sp1_verb
BUILD_DIR := build
SRC_DIR := src

CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE := arm-none-eabi-size

# Compiler flags for nRF52840 Cortex-M4 with Hardware FPU
CFLAGS += -mcpu=cortex-m4
CFLAGS += -mthumb -mabi=aapcs
CFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS += -Wall -Werror -O2 -g3
CFLAGS += -DNRF52840_XXAA -DCONFIG_GPIO_AS_PINRESET
CFLAGS += -ffunction-sections -fdata-sections -fno-strict-aliasing
CFLAGS += -fno-builtin --short-enums
CFLAGS += -I$(SRC_DIR)

# Linker flags
LDFLAGS += -mcpu=cortex-m4
LDFLAGS += -mthumb -mabi=aapcs
LDFLAGS += -mfloat-abi=hard -mfpu=fpv4-sp-d16
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -lm -lc

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean flash

all: $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).hex
	@echo "--- SP-1 Reverb Binary Build Complete ---"
	@$(SIZE) $(BUILD_DIR)/$(TARGET).elf || true
	@echo "Flashable binary ready: $(BUILD_DIR)/$(TARGET).bin"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(BUILD_DIR)
