
timestamp = "$$(date)"

upload: darkmodetoggle.js index.html style.css
	git add -A
	git commit -m $(timestamp)