FROM spicetools/deps
WORKDIR /src
RUN chown user:user /src
USER user
COPY --chown=user:user --from=gitroot . /src/.git
COPY --chown=user:user . /src/src/spice2x
WORKDIR /src/src/spice2x
CMD ["./build_all.sh"]
