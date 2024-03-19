#!/bin/sh
killall Stream\ Deck
cmake --build ./build --config Debug --target all -j 12
cp ./build/speech com.talon.speech.sdPlugin
rm -rf ~/Library/Application\ Support/com.elgato.StreamDeck/Plugins/com.talon.speech.sdPlugin/
cp -R com.talon.speech.sdPlugin ~/Library/Application\ Support/com.elgato.StreamDeck/Plugins/
