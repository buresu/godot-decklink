# godot-decklink
Godot DeckLink Addon via GDExtension

## Install the Desktop Video
Please download and install the driver from the following website.  
https://www.blackmagicdesign.com/jp/developer/products/capture-and-playback/sdk-and-software  

On Linux, you need to enable the DesktopVideoHelper service.  
```
sudo systemctl enable --now DesktopVideoHelper
```

## Build
Download and unzip the Desktop Video SDK.  
https://www.blackmagicdesign.com/jp/developer/products/capture-and-playback/sdk-and-software  

```
git clone --recursive https://github.com/buresu/godot-decklink.git
cd godot-decklink
mkdir build && cd build

[Windows]
cmake -G "Visual Studio 18 2026" .. -DDECKLINK_SDK_DIR=PATH_TO_DECKLINK_SDK
cmake --build . --config [Debug|Release] --target install

[Mac, Linux]
cmake -DCMAKE_BUILD_TYPE=[Debug|Release] -DDECKLINK_SDK_DIR=PATH_TO_DECKLINK_SDK ..
cmake --build . --target install
```

## License
MIT License
