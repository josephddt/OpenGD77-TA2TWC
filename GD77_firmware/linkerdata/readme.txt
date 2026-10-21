To create the empty codec binary sections required as part of the linker process use the codec_cleaner utility in the tools folder

codec_cleaner -C and move the resultant files codec_bin_section_1.bin and codec_bin_section_2.bin to this folder

Note.
For developers intending to upload via SWD to the radio, this method won't work, because the unencrypted sections from offset 0x4400 for 16k
and 0x54000 for 163984 bytes are required. However for legal reasons its impossible to distribute these files.
