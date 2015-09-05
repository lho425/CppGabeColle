が・べ・こ・れ　始まります！

# What is this ?
C++14でライブラリレベルのガベージコレクションを行います。

正確なGCです。

Boehm GC と違い、スタックをスキャンしないので、ポータブルです。

スレッドセーフではありませんが、改良すればマルチスレッド対応できるでしょう。

標準ライブラリのスマートポインタのように使います。

# usage
demo.cppを読んで、実行してみて！

    g++ -std=c++1y demo.cpp && ./a

デバッグ出力を見たい場合は

    g++ -DCPPGBCL_DEBUG_MODE -std=c++1y demo.cpp && ./a

# copy right
LHO425

# license
MIT
