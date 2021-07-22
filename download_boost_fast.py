import wget

url = 'https://boostorg.jfrog.io/artifactory/main/release/1.76.0/source/boost_1_76_0.zip';
wget.download(url, 'boost.zip');
