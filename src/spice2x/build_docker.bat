docker build --pull external/docker -t spicetools/deps
docker build --build-context gitroot=%cd%/../../.git . -t spicetools/spice
docker run --rm -it -v %cd%/dist:/src/src/spice2x/dist -v %cd%/bin:/src/src/spice2x/bin -v %cd%/.ccache:/ccache spicetools/spice %*
@REM to generate PDBs, set DEBUG to 1 in build_all.sh, place cv2pdb in external\cv2pdb, and run below
@REM external\cv2pdb\cv2pdb.exe bin\spice2x\spicecfg.exe bin\spice2x\spicecfg-pdb.exe bin\spice2x\spicecfg-pdb.pdb
@REM external\cv2pdb\cv2pdb.exe bin\spice2x\spice.exe bin\spice2x\spice-pdb.exe bin\spice2x\spice-pdb.pdb
@REM external\cv2pdb\cv2pdb.exe bin\spice2x\spice64.exe bin\spice2x\spice64-pdb.exe bin\spice2x\spice64-pdb.pdb