.PHONY: serve build pdf clean

CHROME ?= $(shell command -v google-chrome || command -v chromium || echo "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
PORT ?= 8765

serve:
	mkdocs serve

build:
	mkdocs build

# Prints the combined page in the light scheme; the time budget lets mermaid render first.
pdf: build
	python3 -m http.server $(PORT) -d site >/dev/null 2>&1 & server=$$!; \
	trap "kill $$server" EXIT; \
	for i in $$(seq 1 50); do curl -s -o /dev/null http://127.0.0.1:$(PORT)/ && break; sleep 0.1; done; \
	"$(CHROME)" --headless --disable-gpu --no-sandbox --blink-settings=preferredColorScheme=1 \
		--no-pdf-header-footer --run-all-compositor-stages-before-draw --virtual-time-budget=20000 \
		--print-to-pdf=site/magda-manual.pdf http://127.0.0.1:$(PORT)/print_page/

clean:
	rm -rf site
