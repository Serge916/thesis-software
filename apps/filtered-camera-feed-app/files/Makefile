APP = filtered-camera-feed-app

# Add any other object files to this list below
APP_OBJS = filtered-camera-feed-app.o dma-api.o helper.o

all: build

build: $(APP)

$(APP): $(APP_OBJS)
	$(CC) -o $@ $(APP_OBJS) $(LDFLAGS) $(LDLIBS)
clean:
	rm -f $(APP) *.o

