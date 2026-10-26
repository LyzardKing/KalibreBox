build:
	cmake -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j$(nproc)

build-android:
	rm -rf /home/user/CraftRoot/tmp/android-arm64-toolchain
	craft --clean apps/kalibrebox
	craft --install-deps apps/kalibrebox
	craft --options apps/kalibrebox.srcDir=/home/user/KalibreBox \
	--configure --make --package apps/kalibrebox

# sign-android:
# 	cp ../build/apps/kalibrebox/work/build/android-build/build/outputs/apk/release/android-build-release-unsigned.apk .
# 	$ANDROID_HOME/build-tools/$ANDROID_BUILD_TOOLS_REVISION/zipalign -p -f -v 4 android-build-release-unsigned.apk kalibrebox.signed.apk
# 	$ANDROID_HOME/build-tools/$ANDROID_BUILD_TOOLS_REVISION/apksigner sign --verbose --ks key.keystore kalibrebox.signed.apk


logcat:
	adb logcat -v threadtime -s AndroidRuntime:E DEBUG:E libc:F Qt:W default:V kalibrebox.core:V