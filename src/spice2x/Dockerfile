FROM spicetools/deps
WORKDIR /src

COPY --from=gitroot . /src/.git
COPY . /src/src/spice2x
WORKDIR /src/src/spice2x
ENTRYPOINT ["./build_all.sh"]
