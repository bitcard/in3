#!/bin/sh

# we check if incubed is installed, if not we try to use the local incubed-directory.
if [ ! -d /usr/local/include/in3 ]; then

  echo "Using local incubed build..."
  if [ ! -d ../../build/lib ]; then
     # not installed yet, so let'sa build it locally
     mkdir -p ../../build
     cd ../../build
     cmake .. && make 
     cd ../c/examples
  fi

  # set the library path to use the local
  BUILDARGS="-L../../build/lib/  -I../../c/include/ ../../build/lib/libin3.a -ltransport_curl -lcurl"
else
  BUILDARGS="-lin3"
fi
# now build the examples build
for f in *.c; 
  do
    if [ "$f" = ledger_sign.c ]; then # skipping ledger_sign compilation as it requires specific dependencies 
      continue
    fi
    gcc -std=c99 -o "${f%%.*}" $f $BUILDARGS -D_POSIX_C_SOURCE=199309L
done

