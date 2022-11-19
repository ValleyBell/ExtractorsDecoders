# Extractors and Decoders

This repository contains Various tools to extract and decompress game archives I wrote over the years.
In most cases the goal was to extract game music.

## CompileMLKTool

This tool extracts and creates `MLK` music archives used by various games developed by the Japanese game developer "Compile".

I developed it to extract the music from "Comet Summoner", which is part of Compile's "DiscStation Vol. 20". It is confirmed to work with all games from that disk.

Notes about the MIDI files:

- Looping has to be enabled using a flag in the MLK archive.
- Custom loop markers are supported. "Control Change 31" is the Loop Start marker. Channel and value do not matter.
- For some reason, "Time Signature" and "Key Signature" meta events are treated as Loop Start markers as well.
- SysEx messages don't seem to work.

## danbidec

This tool decrypts music files used by the Korean game developer "Danbi System".

The files are encrypted by XORing each byte with the low 8 bits of the file position.

It is confirmed to work with the `.D` and `.I` files from "GoGo!! Our Star" / "GoGo Uribyeol".

## DiamondRushExtract

This tool extracts the `.f` archives from the J2ME game "Diamond Rush" developed by Gameloft.

I just wanted to get the MIDIs.

## DIMUnpack

This tool unpacks certain `.DIM` disk image files that I was unable to open with DiskExplorer.

## FoxRangerExtract

This tool extracts music from the archives used by the Korean game developer Soft Action, which was responsible for the "Fox Ranger" series.

The archives themselves are unencrypted, but the unpacked files may need an XOR decryption.

## gensqu\_dec

This tool decompresses files from "Genocide Square" (FM Towns).

Supported are:

- `.ARD` files (archives with compressed files, use `-a` parameter)
- `.CAR` files (single compressed files, use `-f` parameter)

The game uses a custom variant of LZSS.

TODO: support extracting archives with uncompressed files

## kenji\_dec

This is a tool to extract and decompress archives used by the PC-98 adventure/VN engine that has the copyright notice "Programed by KENJI".

The adventure engine was commonly used by games published by Birdy Soft, Discovery and Orange House.

Files whose extension ends with a `1` usually contain only a single file.
Files whose extension ends with a `2` usually are archives and contain multiple files.

It was originally called "twinkle\_dec", because it was written to extract music from "Bunretsu Shugo Shin Twinkle Star".

The compression is standard LZSS with a non-standard initialization for the dictionary, which is the same that Wolf Team uses.

## LBXUnpack

This tool unpacks the `.LBX` files used by the DOS version of "Princess Maker 2".

## mfd\_dec

This is a tool for decrypting the PC-98 sound driver executable `MFD.COM`.

It uses an XOR encryption with a key that changes based on the unencrypted data.

## mrndec

This tool decompresses archives used by the Korean game developer "Mirinae Software".

It was confirmed to work with the .MUE files from their "The Day" series.

## rekiai\_dec

This tool unpacks song archives used by the PC-98 game "Rekiai".

The `.MF` files contain

- the song title
- SSG and OPN instruments 
- MsDrv v4 files for OPN, OPNA and SC-55 MIDI

## wolfteam\_dec

This tool decompresses archives used in games developed by Wolf Team.

The compression is standard LZSS with a non-standard initialization for the dictionary.

## x86k\_sps\_dec

X68000 S.P.S. Archive Unpacker

Confirmed to work with:

- Daimakaimura (TEXTDAT*.SLD)
- Street Fighter II: Champion Edition (FM.BLK, GM.BLK)
- Super Street Fighter II: The New Challengers (FM.BLK, GM.BLK)

Daimakaimura and SSF2 use mostly standard LZSS, but modified to use a BigEndian reference word and not requiring a 4 KB dictionary. (The extracted data is used as reference only.)

Super Street Fighter II uses LZSS with modifications to how the reference word works. `SSF2_Compr.txt` contains a disassembly of its decompression code.

## xordec

A simple tool that XORs the whole file with a user-specified key.

Some Korean game developers (e.g. Soft Action) use a simple XOR to encrypt their files.
