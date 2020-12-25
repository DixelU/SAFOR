from requests import get
def download(url, file_name):
  with open(file_name, "wb") as file:
    response = get(url);
    file.write(response.content);
  
download("https://dl.bintray.com/boostorg/release/1.75.0/source/boost_1_75_0.zip","boost.zip");");
