bin_PROGRAMS = voxelizer viterbi
AM_CXXFLAGS = $(libpng_CFLAGS) $(zlib_CFLAGS) $(CFLAGS) -std=c++0x
voxelizer_SOURCES = voxelizer.cc core.cc pdbplugin.c hmm.c cache.c
voxelizer_LDADD = $(libpng_LIBS) $(zlib_LIBS) -ljson-c -lm $(LDFLAGS)
viterbi_SOURCES = viterbi.cc core.cc pdbplugin.c hmm.c cache.c
viterbi_LDADD = $(libpng_LIBS) $(zlib_LIBS) -ljson-c -lm $(LDFLAGS)
