# Maintainer: xi-ve <zunavs@gmail.com>
pkgname=openvr-lighthouse-manager-linux
pkgver=1.0.0
pkgrel=1
pkgdesc="Linux port of OVR Lighthouse Manager - manage SteamVR base station power via Bluetooth LE. Requires SteamVR to be installed (provides OpenVR headers)."
arch=('x86_64')
url="https://github.com/xi-ve/openvr-lighthouse-manager-linux"
license=('MIT')
install="${pkgname}.install"
depends=('bluez-libs' 'dbus' 'glfw-x11' 'libx11')
makedepends=('cmake' 'base-devel' 'pkgconf')
source=("${pkgname}-${pkgver}.tar.gz::https://github.com/xi-ve/openvr-lighthouse-manager-linux/archive/main.tar.gz"
        "openvr-headers.tar.gz::https://github.com/ValveSoftware/openvr/archive/master.tar.gz"
        "imgui.tar.gz::https://github.com/ocornut/imgui/archive/refs/heads/master.tar.gz")
sha256sums=('SKIP'
            'SKIP'
            'SKIP')

prepare() {
  cd "${srcdir}/${pkgname}-main"
  
  if [ -d "build" ]; then
    rm -rf build
  fi
  
  if [ ! -d "../lib/openvr/headers" ]; then
    echo "Extracting OpenVR headers..."
    mkdir -p ../lib/openvr
    tar -xzf "${srcdir}/openvr-headers.tar.gz" -C ../lib/openvr --strip-components=1 openvr-master/headers 2>/dev/null || true
  fi
  
  if [ ! -d "../WindowsEdition/OpenVR-SpaceCalibrator/lib/imgui" ]; then
    echo "Extracting ImGui..."
    mkdir -p ../WindowsEdition/OpenVR-SpaceCalibrator/lib
    if [ -d "${srcdir}/imgui-master" ]; then
      mv "${srcdir}/imgui-master" ../WindowsEdition/OpenVR-SpaceCalibrator/lib/imgui
    else
      tar -xzf "${srcdir}/imgui.tar.gz" -C ../WindowsEdition/OpenVR-SpaceCalibrator/lib --strip-components=1 imgui-master 2>/dev/null || \
      tar -xzf "${srcdir}/imgui.tar.gz" -C ../WindowsEdition/OpenVR-SpaceCalibrator/lib --strip-components=0 2>/dev/null && \
      mv ../WindowsEdition/OpenVR-SpaceCalibrator/lib/imgui-master ../WindowsEdition/OpenVR-SpaceCalibrator/lib/imgui 2>/dev/null || true
    fi
  fi
}

build() {
  cd "${srcdir}/${pkgname}-main"
  
  export CFLAGS="${CFLAGS//-ffile-prefix-map=* /} -Wno-format-security"
  export CXXFLAGS="${CXXFLAGS//-ffile-prefix-map=* /} -Wno-format-security"
  
  # Use Makefile (handles both CLI and GUI builds with ImGui)
  # Makefile expects OPENVR_DIR to be set relative to project root
  # It will look for ../lib/openvr/headers and ../lib/openvr/lib/linux64
  
  # Build CLI version
  make -j$(nproc)
  
  # Build GUI version
  make -j$(nproc) gui
}

package() {
  cd "${srcdir}/${pkgname}-main"
  
  # Install binaries to /usr/bin
  install -Dm755 build/bin/lighthouse-manager "${pkgdir}/usr/bin/lighthouse-manager"
  install -Dm755 build/bin/lighthouse-manager-gui "${pkgdir}/usr/bin/lighthouse-manager-gui"
  
  # Install manifest and install script to package directory (for install hook)
  install -Dm644 manifest.vrmanifest "${pkgdir}/usr/lib/${pkgname}/manifest.vrmanifest"
  install -Dm755 openvr-lighthouse-manager-install.sh "${pkgdir}/usr/bin/${pkgname}-install"
  
  # Install README
  install -Dm644 README.md "${pkgdir}/usr/share/doc/${pkgname}/README.md"
  
  # Install LICENSE if it exists
  if [ -f LICENSE ]; then
    install -Dm644 LICENSE "${pkgdir}/usr/share/licenses/${pkgname}/LICENSE"
  fi
}

