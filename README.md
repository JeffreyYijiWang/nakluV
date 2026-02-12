# nakluV

This is the starter code for the [nakluV](http://naak.love) inside-out and backward Vulkan tutorial.

It contains code you will modify throughout the tutorial along with a pre-built object files (`pre/*/refsol.o*`) containing code you will replace during the tutorial.

See [step0](http://naak.love/step0/) for information about how to set up your development environment.

Code to resync with AFS

rsync -av --delete \
  --exclude 'bin/' --exclude 'build/' \
  --exclude '*.o' --exclude '*.obj' --exclude '*.exe' --exclude '*.spv' \
  /mnt/c/Users/Jeffr/OneDrive/Documents/GitHub/real_time_rendering/nakluV/ \
  jw8@linux.andrew.cmu.edu:~/A1_src/

push to teh afs
SRC=/afs/andrew.cmu.edu/usr9/jw8/A1_src/
DEST=/afs/cs.cmu.edu/academic/class/15472-s26/jw8/A1/code/

rsync -av --delete \
  --exclude 'bin/' --exclude 'build/' \
  --exclude '*.o' --exclude '*.obj' --exclude '*.exe' --exclude '*.spv' \
  "$SRC"  "$DEST"