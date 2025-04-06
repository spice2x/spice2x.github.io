FROM spicetools/deps
WORKDIR /src

COPY --from=gitroot . /src/.git
COPY . /src/src/spice2x
WORKDIR /src/src/spice2x
CMD ["./build_all.sh"]
