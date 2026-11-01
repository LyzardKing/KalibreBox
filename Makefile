# Optional local settings, see .env.template.
-include .env
export

# Variables
PACKAGE_NAME ?= org.lyzardking.kalibrebox
MAIN_ACTIVITY ?= org.lyzardking.kalibrebox.MainActivity

.PHONY: bump release build clean start-android start-android-arm32 build-android build-android-arm32 \
	sign-android sign-android-arm32 install-android logcat lint lint-cpp lint-qml lint-fix format

build:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$(nproc)

clean:
	rm -rf build

start-android:
	docker run -ti --rm -v "$(CRAFT_ROOT)":/home/user/CraftRoot -v "$(CURDIR)":/home/user/KalibreBox "$(ANDROID_IMAGE)" bash

start-android-arm32:
	docker run -ti --rm -v "$(HOME)/craft-kde-32":/home/user/CraftRoot -v "$(CURDIR)":/home/user/KalibreBox "$(ANDROID_IMAGE)" bash

# Android builds run in KDE's andr oid-qt611 image with a persistent Craft root
# mounted at /home/user/CraftRoot (the same layout as start-android).
# CRAFT_ROOT picks the ABI: ~/craft-kde-android is arm64-v8a, ~/craft-kde-32
# is armeabi-v7a (ABI = android-clang-arm32 in its etc/CraftSettings.ini).
ANDROID_IMAGE ?= invent-registry.kde.org/sysadmin/ci-images/android-qt611
CRAFT_ROOT ?= $(HOME)/craft-kde-android

bump:
	scripts/bump $(version)

release:
	scripts/release-android.sh $(version)

build-android:
	CRAFT_ROOT="$(CRAFT_ROOT)" ANDROID_IMAGE="$(ANDROID_IMAGE)" scripts/build-android.sh

# 32-bit build for devices that only report armeabi-v7a (e.g. Fire tablets).
build-android-arm32:
	$(MAKE) build-android CRAFT_ROOT=$(HOME)/craft-kde-32

# Signs dist/kalibrebox-unsigned.apk into dist/kalibrebox.apk. Uses
# $(CRAFT_ROOT)/tmp/key.keystore unless KEYSTORE is set; the password comes
# from KEYSTORE_PASS or an interactive prompt.
sign-android:
	CRAFT_ROOT="$(CRAFT_ROOT)" ANDROID_IMAGE="$(ANDROID_IMAGE)" scripts/sign-android.sh

sign-android-arm32:
	$(MAKE) sign-android CRAFT_ROOT=$(HOME)/craft-kde-32

install-android:
	adb install -r dist/kalibrebox.apk

logcat:
	adb logcat -v threadtime -s AndroidRuntime:E DEBUG:E libc:F Qt:W default:V kalibrebox.core:V

# Linting needs a configured build dir (compile_commands.json, .clang-format
# from ECM, qmllint target): run `make build` or `cmake -B build` first.
CPP_SOURCES = $(shell git ls-files 'src/*.cpp' 'tests/*.cpp')
CPP_FILES = $(shell git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h')

lint: lint-cpp lint-qml

lint-cpp:
	run-clang-tidy -p build $(CPP_SOURCES)
	clang-format --dry-run --Werror $(CPP_FILES)

lint-qml:
	cmake --build build --target kalibrebox_qmllint

lint-fix:
	run-clang-tidy -p build $(CPP_SOURCES) -fix
	clang-format -i $(CPP_FILES)

# Reformat C++ (KDE's clang-format style) and QML (qmlformat) sources in place.
format:
	cmake --build build --target clang-format qmlformat

## run: Install and launch the app on a connected device
run: install
	@echo "Launching $(PACKAGE_NAME)..."
	$(ADB) shell am start -n $(PACKAGE_NAME)/$(MAIN_ACTIVITY)
	@echo "Launch successful."

	
## emulate: Launch the app in an Android emulator
# sdkmanager "platform-tools" "emulator" "system-images;android-34;google_apis;x86_64"
# avdmanager create avd -n test -k "system-images;android-34;google_apis;x86_64"
AVD_NAME ?= test
ADB = adb
EMULATOR_SDK = $(shell brew --prefix)/share/android-commandlinetools
# Scoped to emulate so gradle builds don't pick up (and download into) the brew SDK
emulate: export ANDROID_HOME = $(EMULATOR_SDK)
emulate: export ANDROID_SDK_ROOT = $(EMULATOR_SDK)
emulate: export PATH := $(EMULATOR_SDK)/cmdline-tools/latest/bin:$(EMULATOR_SDK)/platform-tools:$(EMULATOR_SDK)/emulator:$(PATH)

emulate:
	@echo "Launching emulator..."
	@nohup emulator -avd $(AVD_NAME) >/tmp/emulator.log 2>&1 &
	@echo "Waiting for emulator to boot..."
	$(ADB) wait-for-device
	@until [ "$$($(ADB) shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; do sleep 2; done
	@echo "Installing and launching app on emulator..."
	$(MAKE) run
