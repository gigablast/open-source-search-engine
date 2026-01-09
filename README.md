# open-source-search-engine
An open source web search engine and spider/crawler. This was once the codebase for a search engine called Gigablast, but the site is no longer operational.  This is a fork of the original codebase located at https://github.com/gigablast/open-source-search-engine

# Quick Start
To experiment, you can quickly launch via docker by running:
```
docker run -p 8000:8000 -it --rm moldybits/open-source-search-engine
```

If you wish to preserve data between runs, you can:
```
docker run -p 8000:8000 -it --rm -v $(pwd)/data:/var/gigablast/data0 moldybits/open-source-search-engine
```

# Major changes in this fork
* cleanup! - Moved sources that are actually used into `src` dir.  Everything else has been stuffed in the `junkdrawer` dir.
* More cleanup - formatting, removing TONS of commented code, fixing some segfaults.  This is ongoing...
* I have replaced the original `Makefile` with CMake.  This now installs the correct files required so you can execute `./gb` in the `build` directory and run a test server there without it borking your source dir.
* Stubbed out some testing functionality for building tests if this ever gets cleaned up enough to start making "real" changes.

# Building
The recommended environment is still x86_64 Linux. An experimental macOS path now exists (tested on Apple Silicon) that replaces the legacy Linux signal loop with a kqueue-driven event loop so you can run the crawler natively. There might be rough edges, but it is usable for local development and testing.

### Install Catch2
```
git clone https://github.com/catchorg/Catch2.git
cd Catch2
cmake -Bbuild -H. -DBUILD_TESTING=OFF
sudo cmake --build build/ --target install
```

### Debian or Ubuntu
```
sudo apt-get install make g++ libssl-dev libz-dev cmake
```

### RedHat or AlmaLinux
Last tried with AlmaLinux 9
```
sudo yum install gcc-c++ openssl-devel libz-devel cmake
```

### Build
```
cd open-source-search-engine
cmake -Bbuild
cmake --build build/
```

# Issues & Pull Requests
Should be filed at https://github.com/twistdroach/open-source-search-engine

# Testing
Tests can be put in the tests directory.  I have written a few simple examples just to make sure it (mostly) works.

# Documentation
There are various docs located in the html directory.  The FAQ & developer.html are particularly interesting.
