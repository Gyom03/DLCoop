# Relais DLCoop en conteneur (variante du service systemd) :
#   docker build -t dlcoop-relais tools/relais
#   docker run -d --restart unless-stopped -p 20300:20300/udp -e RELAIS_MOTS_DE_PASSE=... dlcoop-relais
FROM alpine:3.20 AS build
RUN apk add --no-cache g++
COPY relais.cpp /src/
RUN g++ -O2 -std=c++17 -static -o /relais /src/relais.cpp

FROM scratch
COPY --from=build /relais /relais
EXPOSE 20300/udp
USER 65534:65534
ENTRYPOINT ["/relais"]
