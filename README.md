# godot-decklink
Godot DeckLink Addon via GDExtension

## Build
Download and unzip Desktop Video SDK.
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
