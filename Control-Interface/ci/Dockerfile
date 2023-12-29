# base image
FROM node:18 as builder

# set working directory
RUN mkdir /usr/src/app
ADD . /usr/src/app
WORKDIR /usr/src/app

# add `/usr/src/app/node_modules/.bin` to $PATH
ENV PATH /usr/src/app/node_modules/.bin:$PATH

# install and cache app dependencies
RUN npm install -g @angular/cli
RUN yarn
RUN ng build --configuration=production

FROM nginx:latest
COPY --from=builder /usr/src/app/dist/train-interface /usr/share/nginx/html
#COPY ci/default.conf /etc/nginx/conf.d/default.conf
