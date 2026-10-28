#!/bin/sh

echo
echo "Corpus statistics"
echo "------------------------------------------------------------"
./build/mse stats --manifest data/manifest.tsv
./build/mse stats --manifest data2/data/manifest.tsv

echo
echo "Build the Shakespeare index"
echo "------------------------------------------------------------"
./build/mse build \
  --manifest data/manifest.tsv \
  --index build/index.bin \
  --dict-file build/index.bpt \
  --stop-words data/stopwords.txt \
  --stem

echo
echo "Shakespeare search examples"
echo "------------------------------------------------------------"
echo "Query word 'love' and return the top 5 results:"
./build/mse query \
  --index build/index.bin \
  --word love \
  --top-x 5 \
  --pretty \
  --stop-words data/stopwords.txt
echo "---"
echo "Query phrase 'good night' and return the top 3 results:"
./build/mse query \
  --index build/index.bin \
  --phrase "good night" \
  --top-x 3 \
  --pretty \
  --stop-words data/stopwords.txt
echo "---"
echo "Query words 'cabbage rhinoceros' with OR:"
./build/mse query \
  --index build/index.bin \
  --words "cabbage rhinoceros" \
  --or \
  --top-x 5 \
  --pretty \
  --stop-words data/stopwords.txt

echo
echo "Build the RFC index"
echo "------------------------------------------------------------"
./build/mse build \
  --manifest data2/data/manifest.tsv \
  --index build/rfc-index.bin \
  --dict-file build/rfc-index.bpt \
  --stem

echo
echo "RFC search examples"
echo "------------------------------------------------------------"
echo "Query word 'domain' and return the top 3 results in JSON format:"
./build/mse query \
  --index build/rfc-index.bin \
  --word domain \
  --top-x 3 \
  --json
echo "---"
echo "Query phrase 'domain name' and return the top 3 results in JSON format:"
./build/mse query \
  --index build/rfc-index.bin \
  --phrase "domain name" \
  --top-x 3 \
  --json
