FROM spicetools/deps
WORKDIR /src
RUN chown user:user /src
USER user
COPY --chown=user:user . /src
CMD ["./build_all.sh"]
