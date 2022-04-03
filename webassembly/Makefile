
bare_metal_wasm.wasm : graphics.c
	clang --target=wasm32 -O2 -nostdlib "-Wl,--no-entry" "-Wl,--export-all" -o $(@) $(^)

clean :
	rm -f bare_metal_wasm.wasm

show :
	python3 -m http.server