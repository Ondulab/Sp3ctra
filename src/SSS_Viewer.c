/*
 ============================================================================
 Name        : SSS_Viewer.c
 Author      : ZHONX
 Version     :
 Copyright   : Your copyright notice
 Description : SSS image visualization
 ============================================================================
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include<arpa/inet.h>
#include<sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define UDP_HEADER								(1397969715) //01010011 01010011 01010011 00110011 SSS3
#define UDP_HEADER_SIZE							(1)//uint32
#define CIS_ACTIVE_PIXELS_PER_LINE				(1152)
#define CIS_LINE_LEN 							(CIS_ACTIVE_PIXELS_PER_LINE * 3)

#define BUFLEN 									((CIS_LINE_LEN + UDP_HEADER_SIZE) * 2)	//Max length of buffer
#define PORT 									(55151)	//The port on which to listen for incoming data

#define WINDOWS_WIDTH							(CIS_LINE_LEN)
#define WINDOWS_HEIGHT							(2160)


//memo on linux terminal : sudo nc -u -l 55151

void die(char *s)
{
	perror(s);
	exit(1);
}

int main(void)
{
	struct sockaddr_in si_me, si_other;

	int s = 0;
	int y = 0;
	int recv_len = 0;
	unsigned int slen = sizeof(si_other);
	uint32_t buf[BUFLEN];

	//--------------------------------------SDL2 INIT-------------------------------------------//
	SDL_Window *window = NULL;
	SDL_Renderer *renderer = NULL;
	SDL_Texture* background_texture;
	SDL_Texture* foreground_texture;
	int statut = EXIT_FAILURE;

	/* Rectangles for drawing which will specify source (inside the texture)
	  and target (on the screen) for rendering our textures. */
	SDL_Rect SrcR;
	SDL_Rect DestR;

	SrcR.x = 0;
	SrcR.y = 0;
	SrcR.w = WINDOWS_WIDTH;
	SrcR.h = WINDOWS_HEIGHT - 1;

	DestR.x = 0;
	DestR.y = 1;
	DestR.w = WINDOWS_WIDTH;
	DestR.h = WINDOWS_HEIGHT - 1;

	/* Initialisation, création de la fenêtre et du renderer. */
	if(0 != SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "Erreur SDL_Init : %s", SDL_GetError());
		goto Quit;
	}
	window = SDL_CreateWindow("SDL2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			WINDOWS_WIDTH, WINDOWS_HEIGHT, SDL_WINDOW_SHOWN);
	if(NULL == window)
	{
		fprintf(stderr, "Erreur SDL_CreateWindow : %s", SDL_GetError());
		goto Quit;
	}
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if(NULL == renderer)
	{
		fprintf(stderr, "Erreur SDL_CreateRenderer : %s", SDL_GetError());
		goto Quit;
	}

	SDL_SetWindowTitle(window, "SSS_Viewer");

	//	SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);

	if(NULL == window)
	{
		fprintf(stderr, "Erreur SDL_CreateWindow : %s", SDL_GetError());
		return EXIT_FAILURE;
	}

	background_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET, WINDOWS_WIDTH, WINDOWS_HEIGHT);
	foreground_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET, WINDOWS_WIDTH, WINDOWS_HEIGHT);

	SDL_SetRenderTarget(renderer, background_texture);

	//---------------------------------------UDP INIT--------------------------------------------//

	//create a UDP socket
	if ((s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
	{
		die("socket");
	}

	// zero out the structure
	memset((char *) &si_me, 0, sizeof(si_me));

	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(PORT);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	//bind socket to port
	if( bind(s , (struct sockaddr*)&si_me, sizeof(si_me) ) == -1)
	{
		die("bind");
	}

	//---------------------------------------MAIN LOOP-------------------------------------------//
	//keep listening for data
	while(1)
	{
		//		printf("Waiting for data...");
		fflush(stdout);

		//try to receive some data, this is a blocking call
		if ((recv_len = recvfrom(s, buf, BUFLEN * 2, 0, (struct sockaddr *) &si_other, &slen)) == -1)
		{
			die("recvfrom()");
		}

		//print details of the client/peer and the data received
		//		printf("Received packet from %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
		//		printf("Data: %s\n" ,(uint8_t *)buf);

		int max = 0;
		int offset = 0;

		for (int i = 0; i < (BUFLEN / 2); i++)
		{
			if(buf[i] > max)
			{
				max = buf[i];
				offset = i;
			}
		}

		offset += UDP_HEADER_SIZE;

		//				y++;

		for (int x = 0; x < CIS_LINE_LEN; x++)
		{
			SDL_SetRenderDrawColor(renderer, buf[offset + x] / 256, buf[offset+ x] / 256, buf[offset + x] / 256, 255);
			SDL_RenderDrawPoint(renderer, WINDOWS_WIDTH - x, y % WINDOWS_HEIGHT);
		}

		SDL_SetRenderTarget(renderer, foreground_texture);// Dorénavent, on modifie à nouveau le renderer

		SDL_Rect position;
		position.x = 0;
		position.y = 0;
		SDL_QueryTexture(background_texture, NULL, NULL, &position.w, &position.h);
		SDL_RenderCopy(renderer, background_texture, NULL, &position);

		position.x = 0;
		position.y = 1;
		SDL_QueryTexture(background_texture, NULL, NULL, &position.w, &position.h);
		SDL_RenderCopy(renderer, background_texture, NULL, &position);

		SDL_SetRenderTarget(renderer, NULL);

		position.x = 0;
		position.y = 0;
		SDL_QueryTexture(foreground_texture, NULL, NULL, &position.w, &position.h);
		SDL_RenderCopy(renderer, foreground_texture, NULL, &position);

		SDL_RenderPresent(renderer);

		SDL_SetRenderTarget(renderer, background_texture);

		position.x = 0;
		position.y = 0;
		SDL_QueryTexture(foreground_texture, NULL, NULL, &position.w, &position.h);
		SDL_RenderCopy(renderer, foreground_texture, NULL, &position);
	}

	close(s);

	SDL_Delay(1000);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return EXIT_SUCCESS;

	Quit:
	if(NULL != renderer)
		SDL_DestroyRenderer(renderer);
	if(NULL != window)
		SDL_DestroyWindow(window);
	SDL_Quit();
	return statut;
}


#define SHAPE_SIZE 16
