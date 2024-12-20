#!/bin/bash

# Für jede Datei im Verzeichnis
for file in *; do
    # Prüfen, ob es sich um eine reguläre Datei handelt und ob sie nicht bereits mit .gz endet
    if [[ -f "$file" && ! "$file" == *.gz ]]; then
        # Die Datei mit gzip komprimieren und die Ausgabe in eine neue Datei mit .gz-Endung umleiten
        gzip -c "$file" > "${file}.gz"
        echo "Komprimiert: $file -> ${file}.gz"
    fi
done

echo "Komprimierung abgeschlossen."