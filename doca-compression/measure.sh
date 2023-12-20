TEXT_BASE=/usr/share/dict/words
LARGE_TEXT=../../bdev-passthru-compress/compress/input_file_words_1g
INPUT_FILE=input.txt

gen_words_1g() {
    while true; do cat $TEXT_BASE; done | dd of=$LARGE_TEXT bs=4K count=1024 iflag=fullblock
}

make clean
make 

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=1 iflag=fullblock
./doca_compression_local

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((256 / 4)) iflag=fullblock
./doca_compression_local

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 / 4)) iflag=fullblock
./doca_compression_local

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 * 8 / 4)) iflag=fullblock
./doca_compression_local

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 * 64 / 4)) iflag=fullblock
./doca_compression_local

dd if=$LARGE_TEXT of=$INPUT_FILE bs=4K count=$((1024 * 128 / 4)) iflag=fullblock
./doca_compression_local
