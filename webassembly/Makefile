
bare_metal_wasm.wasm : graphics.c
	clang --target=wasm32 -O3 -flto -nostdlib \
		-mbulk-memory -msimd128 -mmutable-globals \
		"-Wl,--no-entry" "-Wl,--export-all" "-Wl,--lto-O3" "-Wl,--strip-all" \
		-o $(@) $(^)

clean :
	rm -f bare_metal_wasm.wasm

show :
	python3 -m http.server