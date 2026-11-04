IOS_CC		= xcrun -sdk iphoneos gcc
MACOSX_CC	= xcrun -sdk macosx gcc

IOS_HEADERS_PATH	= ios_headers

CFLAGS			= -I./include -Wall
CFLAGS			+= -Os
IOS_FLAGS		= -I./$(IOS_HEADERS_PATH) -DIPHONEOS_ARM -miphoneos-version-min=9.0

LDFLAGS			= -framework IOKit -framework CoreFoundation

IOS_ARCH		= -arch armv7 -arch arm64

RA1NPOC_MACOSX_OBJ	= ra1npoc_macosx
RA1NPOC_IOS_OBJ		= ra1npoc_ios

CODESIGN	= codesign -f -s - --entitlements ent.xml
STRIP		= strip

GEN_PAYLOAD	= builtin.sh

SOURCE		= \
			main.c \
			io/iousb.c \
			common/common.c \
			exploit/checkm8_arm64.c


ENTERDFU_MACOSX_OBJ	= enterdfu_macosx
ENTERDFU_IOS_OBJ	= enterdfu_ios

ENTERDFU_SOURCE	= \
               	enterdfu.c \
                io/iousb.c \
                common/common.c

.PHONY: all builtin enterdfu clean

all: builtin enterdfu

get_ios_header:
	$(RM) -r $(IOS_HEADERS_PATH)
	mkdir $(IOS_HEADERS_PATH)
	ln -s $(shell xcrun -sdk macosx -show-sdk-path)/usr/include/libkern $(IOS_HEADERS_PATH)
	ln -s $(shell xcrun -sdk macosx -show-sdk-path)/System/Library/Frameworks/IOKit.framework/Headers $(IOS_HEADERS_PATH)/IOKit

builtin: get_ios_header
	./$(GEN_PAYLOAD)
	# macosx
	$(MACOSX_CC) $(CFLAGS) $(LDFLAGS) $(SOURCE) -o $(RA1NPOC_MACOSX_OBJ)
	# ios
	$(IOS_CC) $(CFLAGS) $(IOS_FLAGS) $(LDFLAGS) $(SOURCE) $(IOS_ARCH) -o $(RA1NPOC_IOS_OBJ)
	$(STRIP) $(RA1NPOC_IOS_OBJ)
	$(CODESIGN) $(RA1NPOC_IOS_OBJ)

enterdfu: get_ios_header
	$(MACOSX_CC) $(CFLAGS) $(LDFLAGS) $(ENTERDFU_SOURCE) -o $(ENTERDFU_MACOSX_OBJ)
	$(IOS_CC) $(CFLAGS) $(IOS_FLAGS) $(LDFLAGS) $(ENTERDFU_SOURCE) $(IOS_ARCH) -o $(ENTERDFU_IOS_OBJ)
	$(STRIP) $(ENTERDFU_IOS_OBJ)
	$(CODESIGN) $(ENTERDFU_IOS_OBJ)

clean:
	-$(RM) $(RA1NPOC_MACOSX_OBJ) $(RA1NPOC_IOS_OBJ) $(ENTERDFU_MACOSX_OBJ) $(ENTERDFU_IOS_OBJ)
	-$(RM) -r $(IOS_HEADERS_PATH)
