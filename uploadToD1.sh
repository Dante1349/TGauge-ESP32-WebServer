cd Control-Interface && \
npm i && \
ng build --configuration=production && \
cd .. && \
rm -rf data && \
mkdir data && \
cp -R ./Control-Interface/dist/browser/* ./data/ && \
pio run -t uploadfs && \
pio run -t upload --upload-port=/dev/ttyUSB0 && pio device monitor
