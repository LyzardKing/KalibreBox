build:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$(nproc)

start-android:
	docker run -ti --rm -v ~/craft-kde-android:/home/user/CraftRoot -v ~/Documents/Projects/KalibreBox:/home/user/KalibreBox invent-registry.kde.org/sysadmin/ci-images/android-qt611 bash

start-android-arm32:
	docker run -ti --rm -v ~/craft-kde-32:/home/user/CraftRoot -v ~/Documents/Projects/KalibreBox:/home/user/KalibreBox invent-registry.kde.org/sysadmin/ci-images/android-qt611 bash

# Android builds run in KDE's android-qt611 image with a persistent Craft root
# mounted at /home/user/CraftRoot (the same layout as start-android).
# CRAFT_ROOT picks the ABI: ~/craft-kde-android is arm64-v8a, ~/craft-kde-32
# is armeabi-v7a (ABI = android-clang-arm32 in its etc/CraftSettings.ini).
ANDROID_IMAGE ?= invent-registry.kde.org/sysadmin/ci-images/android-qt611
CRAFT_ROOT ?= $(HOME)/craft-kde-android

define android_craft
docker run --rm -ti \
	-v "$(CRAFT_ROOT)":/home/user/CraftRoot \
	-v "$(CURDIR)":/home/user/KalibreBox \
	--entrypoint bash $(ANDROID_IMAGE) -c \
	'source /home/user/CraftRoot/craft/craftenv.sh && $(1)'
endef

build-android:
	$(call android_craft,craft --clean apps/kalibrebox && \
	craft --install-deps apps/kalibrebox && \
	craft --options apps/kalibrebox.srcDir=/home/user/KalibreBox \
	--configure --make apps/kalibrebox && \
	cd /home/user/CraftRoot/build/apps/kalibrebox/work/build && \
	cmake --install . --prefix $$PWD/android-icons && \
	rm -f android-build/kalibrebox.apk && cmake --build . --target apk)

# 32-bit build for devices that only report armeabi-v7a (e.g. Fire tablets).
build-android-arm32:
	$(MAKE) build-android CRAFT_ROOT=$(HOME)/craft-kde-32

# Signs the release APK from the Craft root's tmp/ (where `craft --package`
# puts it) with tmp/key.keystore; apksigner asks for the keystore password.
sign-android:
	$(call android_craft,cd /home/user/CraftRoot/tmp && \
	cp /home/user/CraftRoot/build/apps/kalibrebox/work/build/android-build/build/outputs/apk/release/android-build-release-unsigned.apk /home/user/CraftRoot/tmp && \
	$$ANDROID_HOME/build-tools/$$ANDROID_BUILD_TOOLS_REVISION/zipalign -p -f -v 4 android-build-release-unsigned.apk kalibrebox.signed.apk && \
	$$ANDROID_HOME/build-tools/$$ANDROID_BUILD_TOOLS_REVISION/apksigner sign --verbose --ks key.keystore kalibrebox.signed.apk)
	cp $(CRAFT_ROOT)/tmp/kalibrebox.signed.apk .

sign-android-arm32:
	$(MAKE) sign-android CRAFT_ROOT=$(HOME)/craft-kde-32

install-android:
	adb install -r $(CRAFT_ROOT)/tmp/kalibrebox.signed.apk

logcat:
	adb logcat -v threadtime -s AndroidRuntime:E DEBUG:E libc:F Qt:W default:V kalibrebox.core:V

# Linting needs a configured build dir (compile_commands.json, .clang-format
# from ECM, qmllint target): run `make build` or `cmake -B build` first.
CPP_SOURCES = $(shell git ls-files 'src/*.cpp' 'tests/*.cpp')
CPP_FILES = $(shell git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h')

lint: lint-cpp lint-qml

lint-cpp:
	clang-tidy -p build $(CPP_SOURCES)
	clang-format --dry-run --Werror $(CPP_FILES)

lint-qml:
	cmake --build build --target kalibrebox_qmllint

# Reformat C++ (KDE's clang-format style) and QML (qmlformat) sources in place.
format:
	cmake --build build --target clang-format qmlformat
	