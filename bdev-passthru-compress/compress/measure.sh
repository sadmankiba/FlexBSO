TEXT_BASE=/usr/share/dict/words
LARGE_TEXT=input_file_1g
INPUT_FILE=input_file

gen_words_1g() {
    while true; do cat $TEXT_BASE; done | dd of=$LARGE_TEXT bs=4K count=1 iflag=fullblock
}

make -f Makefile-single clean 
make -f Makefile-single

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=1 iflag=fullblock
./doca_compression_local sw hw 
./doca_compression_local hw hw


dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((256 / 4)) iflag=fullblock
./doca_compression_local sw hw
./doca_compression_local hw hw

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 / 4)) iflag=fullblock
./doca_compression_local sw hw
./doca_compression_local hw hw

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 * 8 / 4)) iflag=fullblock
./doca_compression_local sw hw
./doca_compression_local hw hw

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 * 64 / 4)) iflag=fullblock
./doca_compression_local sw hw
./doca_compression_local hw hw

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 * 256 / 4)) iflag=fullblock
./doca_compression_local sw hw
./doca_compression_local hw hw

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 * 1024 / 4)) iflag=fullblock
./doca_compression_local sw hw
./doca_compression_local hw hw