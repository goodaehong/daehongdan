# 컴파일러 설정
CXX = g++

# C++ 버전 및 OpenCV 헤더 경로 설정
CXXFLAGS = -std=c++17 -Wall `pkg-config --cflags opencv4`

# OpenCV 라이브러리 링크 설정
LDFLAGS = `pkg-config --libs opencv4`

# 생성할 실행 파일 이름
TARGET = evac_server

# 컴파일할 소스 파일들
SRCS = server.cpp EvacPlanner.cpp

# 소스 파일을 기반으로 생성될 오브젝트 파일들(.o)
OBJS = $(SRCS:.cpp=.o)

# 기본 빌드 타겟
all: $(TARGET)

# 실행 파일 생성 규칙
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# 각각의 .cpp 파일을 .o 파일로 컴파일하는 규칙
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 빌드된 오브젝트 파일과 실행 파일 삭제
clean:
	rm -f $(OBJS) $(TARGET)

# 프로그램 실행 시 생성된 결과물 파일들(txt, png) 삭제
clean_data:
	rm -f evac_bitmap.txt evac_debug*.txt evac_preview.png evac_routes.txt