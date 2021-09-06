#include "PongMode.hpp"

//for the GL_ERRORS() macro:
#include "gl_errors.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>

#include <random>
#include<chrono>

#define MOVING_PAD 0
#define STATIONARY_PAD 1
#define TOO_CLOSE 2

//Returns gameState so it is accesible to main
bool PongMode::curGameState() {
	return gameState;
}

//Points is the current point count of player (just left_points)
void PongMode::newGate(unsigned int points) {

	//Setting level and gap params
	unsigned int level = (points / levelPoints % 10) + 1; //In game level (goes up to 10)
	if (points / levelPoints >= 10) useEarlier = true;
	if (points / levelPoints >= 20) moveBlocks = true;
	if(points / levelPoints >= 20) bottomBlock.x = newRightBlock.x;
	float ratio = (maxGap - minGap) / 10.f; //How much to decreases size per level
	float curGap = maxGap - (float) level * ratio; //Update gap and cap at min (level 10 +)
	if (curGap <= minGap) curGap = minGap; 

	//Generating random gate
	unsigned seed = (unsigned int) std::chrono::system_clock::now().time_since_epoch().count(); //Creating seed,
	//Found seed function from http://www.cplusplus.com/reference/random/uniform_real_distribution/operator()/
	std::uniform_real_distribution < double > dist(0.0, 100.0 * (double) maxTop - 100.0*(double)(curGap + minBottom));
	float curTop = (float) dist(std::default_random_engine(seed))/100.f + curGap + minBottom; //Randomly make new gate (top of gate gap)
	dist.reset();
	
	//This lambda creates a new gate top for the before gate based on the after gate's top.
	//@return - float, the percentage of the screen's y that the before gate's top is at
	//@param - curTop - after gate's top in percentage, curGap - what percentage of the screen's y the gap should take
	//seed - Seed created before to be used for random function (based on system clock)
	auto givenBackTop = [this](float curTop, float curGap, unsigned seed) {
		std::uniform_real_distribution < double > distDiv(1.5, 3.0);
		//seedRes is intended to give a range of feasible but dynamic offsets for the before gap compared to after gap
		float seedRes = (float)distDiv(std::default_random_engine(seed));

		//See if putting gap above or below after goes out of bounds. If so, don't use
		//Above gap is - yDivX ration * the x offset + a randomized 1/seedRes fraction of curGap above the after's gap
		//Bottom is similar but below instead of above
		bool justBottom = false;
		float beforeUp = curTop + yDivXOffset * defXOffset + curGap/seedRes;
		if (beforeUp >= maxTop + 0.03333) justBottom = true; //Error to make edge cases feasible without limiting the possible places for second gate
		bool justTop = false;
		float beforeDown = curTop - yDivXOffset * defXOffset - curGap/seedRes;
		if (beforeDown - curGap <= minBottom) justTop = true;

		assert(justTop || beforeDown >= minBottom + curGap);
		assert(beforeUp >= minBottom + curGap);
		if (justBottom)return beforeDown;
		if (justTop) return beforeUp;

		//If both are possible, do a coin flip to decide if to do above or below
		assert(beforeDown >= minBottom + curGap && beforeUp >= minBottom + curGap && beforeUp > beforeDown);
		std::uniform_real_distribution < double > dist(0.0, 1.0);
		if(dist(std::default_random_engine(seed)) >= 0.5) return beforeDown;
		return beforeUp;
	};

	//Creating gate coordinates
	topRadius = glm::vec2(gateWidth, (1.0f - curTop) * court_radius.y + minBottom / 2); 
	bottomRadius = glm::vec2(gateWidth, (curTop - curGap) * court_radius.y);
	float topY = ((1.0f - curTop)/2 + curTop) * 2 * court_radius.y - court_radius.y;
	topCenter = glm::vec2(gateX, topY);
	float bottomY = bottomRadius.y - court_radius.y;
	bottomCenter = glm::vec2(gateX, bottomY);
	assert(bottomY - bottomRadius.y <= -0.499*court_radius.y);

	//Creating earlier gate coordinates based off of first gate coordinates
	float curTopB = givenBackTop(curTop, curGap, seed);
	assert(curTopB - curGap >= minBottom - 0.0005f);
	//Creating actual coordinates based on new top
	topRadiusB = glm::vec2(gateWidth, (1.0f - curTopB) * court_radius.y + minBottom / 2);
	bottomRadiusB = glm::vec2(gateWidth, (curTopB - curGap) * court_radius.y);
	float topYB = ((1.0f - curTopB) / 2 + curTopB) * 2 * court_radius.y - court_radius.y;
	topCenterB = glm::vec2(gateX - defXOffset*2*court_radius.x - 2 *gateWidth, topYB);
	float bottomYB = bottomRadiusB.y - court_radius.y;
	bottomCenterB = glm::vec2(gateX - defXOffset * 2 * court_radius.x - 2 * gateWidth, bottomYB);
	assert(bottomYB - bottomRadiusB.y <= -0.499 * court_radius.y);
	assert(topYB > bottomYB);
	if (useEarlier) { //Make sure gap isn't right where player is to avoid cheating
		if (recurLimit < 10 && abs(curTopB - curGap / 2 - left_paddle.y) <= curGap / TOO_CLOSE) {
			recurLimit++;
			newGate(left_score);
		}
	}
	else {
		if (recurLimit < 10 && abs(curTop - curGap / 2 - left_paddle.y) <= curGap / TOO_CLOSE) {
			recurLimit++;
			newGate(left_score);
		}
	}
	recurLimit = 0; //Avoid infinite recursion
}

PongMode::PongMode() {

	gameState = true; //Game should always play if the object is constructed 

	//Set up gate parameters
	newGate(left_score);

	//set up trail as if ball has been here for 'forever':
	ball_trail.clear();
	ball_trail.emplace_back(ball, trail_length);
	ball_trail.emplace_back(ball, 0.0f);

	
	//----- allocate OpenGL resources -----
	{ //vertex buffer:
		glGenBuffers(1, &vertex_buffer);
		//for now, buffer will be un-filled.

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //vertex array mapping buffer for color_texture_program:
		//ask OpenGL to fill vertex_buffer_for_color_texture_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);

		//set vertex_buffer_for_color_texture_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		//set up the vertex array object to describe arrays of PongMode::Vertex:
		glVertexAttribPointer(
			color_texture_program.Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		glEnableVertexAttribArray(color_texture_program.Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program.Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program.Color_vec4);

		glVertexAttribPointer(
			color_texture_program.TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program.TexCoord_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //solid white texture:
		//ask OpenGL to fill white_tex with the name of an unused texture object:
		glGenTextures(1, &white_tex);

		//bind that texture object as a GL_TEXTURE_2D-type texture:
		glBindTexture(GL_TEXTURE_2D, white_tex);

		//upload a 1x1 image of solid white to the texture:
		glm::uvec2 size = glm::uvec2(1,1);
		std::vector< glm::u8vec4 > data(size.x*size.y, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		//set filtering and wrapping parameters:
		//(it's a bit silly to mipmap a 1x1 texture, but I'm doing it because you may want to use this code to load different sizes of texture)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		//since texture uses a mipmap and we haven't uploaded one, instruct opengl to make one for us:
		glGenerateMipmap(GL_TEXTURE_2D);

		//Okay, texture uploaded, can unbind it:
		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
}

PongMode::~PongMode() {

	//----- free OpenGL resources -----
	glDeleteBuffers(1, &vertex_buffer);
	vertex_buffer = 0;

	glDeleteVertexArrays(1, &vertex_buffer_for_color_texture_program);
	vertex_buffer_for_color_texture_program = 0;

	glDeleteTextures(1, &white_tex);
	white_tex = 0;
}

bool PongMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_MOUSEMOTION) {
		//convert mouse from window pixels (top-left origin, +y is down) to clip space ([-1,1]x[-1,1], +y is up):
		glm::vec2 clip_mouse = glm::vec2(
			(evt.motion.x + 0.5f) / window_size.x * 2.0f - 1.0f,
			(evt.motion.y + 0.5f) / window_size.y *-2.0f + 1.0f
		);
		left_paddle.y = (clip_to_court * glm::vec3(clip_mouse, 1.0f)).y;
	}

	return false;
}

void PongMode::update(float elapsed) {

	static std::mt19937 mt; //mersenne twister pseudo-random number generator

	//----- paddle update -----

	left_paddle.y = std::max(left_paddle.y, -court_radius.y + paddle_radius.y);
	left_paddle.y = std::min(left_paddle.y,  court_radius.y - paddle_radius.y);


	//----- ball update -----

	//speed of ball doubles every (1/2 of total needef or level up) points for each level up, before slowing 3/4 with the next level:
	int speedMultVal = ((left_score) / (3 * levelPoints));
	if (left_score / levelPoints / 10 == 1 || left_score / levelPoints / 10 == 2) speedMultVal  = (left_score % (levelPoints * 10)) / (3*levelPoints);
	else if (left_score / levelPoints / 10 >= 3)  speedMultVal = (left_score - 3* (levelPoints * 10)) / (3 * levelPoints);
	float speed_multiplier = 4.0f * std::pow(1.3333f, (float) speedMultVal);

	//velocity cap, though (otherwise ball can pass through paddles):
	speed_multiplier = std::min(speed_multiplier, 10.0f);

	ball += elapsed * speed_multiplier * ball_velocity;

	if (moveBlocks) { //Only update block pos after level 20
		if (topBlock.y + block_radius.y >= maxTop * 2 * court_radius.y - court_radius.y) leftUp = false; //Reset y direction if bounds are hit
		else if (topBlock.y - block_radius.y <= minBottom * 2 * court_radius.y - court_radius.y) leftUp = true;
		if (bottomBlock.y + block_radius.y >= maxTop * 2 * court_radius.y - court_radius.y) rightUp = false;
		else if (bottomBlock.y - block_radius.y <= minBottom * 2 * court_radius.y - court_radius.y) rightUp = true;

		if (leftUp) topBlock.y += elapsed * blockUpdate; //Depending on direction, update left and right blocks y based on elapsed delta in position
		else topBlock.y -= elapsed * blockUpdate;
		if (rightUp) bottomBlock.y += elapsed * blockUpdate;
		else bottomBlock.y -= elapsed * blockUpdate;
	}

	//---- collision handling ----

	//Reset ball position along with new gate

	auto moveBallLeft = [this]() {
		ball = glm::vec2(-1.2f,left_paddle.y);
		ball_velocity = glm::vec2(-1.0f, 0.0f);
		if (ball.y < -court_radius.y + paddle_radius.y + ball_radius.y) ball.y = 1.1f * paddle_radius.y + ball_radius.y - court_radius.y;
		if (ball.y > court_radius.y - paddle_radius.y - ball_radius.y) ball.y = -1.1f * paddle_radius.y - ball_radius.y + court_radius.y;

	};

	//Sees purely if there is an overlap, ie collision, between balls and both gates
	auto gateCollide = [this]() {
		//After
		//Top
		glm::vec2 radius = topRadius;
		glm::vec2 min = glm::max(topCenter - radius, ball - ball_radius);
		glm::vec2 max = glm::min(topCenter + radius, ball + ball_radius);
		if (!(min.x > max.x || min.y > max.y)) return true;
		//Bottom
		radius = bottomRadius;
		min = glm::max(bottomCenter - radius, ball - ball_radius);
		max = glm::min(bottomCenter + radius, ball + ball_radius);
		if (!(min.x > max.x || min.y > max.y)) return true;
		//Before
		//Top
		radius = topRadiusB;
		min = glm::max(topCenterB - radius, ball - ball_radius);
		max = glm::min(topCenterB + radius, ball + ball_radius);
		if (!(min.x > max.x || min.y > max.y) && useEarlier) return true;
		//Bottom
		radius = bottomRadiusB;
		min = glm::max(bottomCenterB - radius, ball - ball_radius);
		max = glm::min(bottomCenterB + radius, ball + ball_radius);
		if (!(min.x > max.x || min.y > max.y) && useEarlier) return true;
		return false;
		
	};

	//paddles:
	auto paddle_vs_ball = [this](glm::vec2 const &paddle, int whichPad) {
		//compute area of overlap:
		glm::vec2 radius = paddle_radius;
		if (whichPad == STATIONARY_PAD) radius = block_radius;
		glm::vec2 min = glm::max(paddle - radius, ball - ball_radius);
		glm::vec2 max = glm::min(paddle + radius, ball + ball_radius);

		//if no overlap, no collision:
		if (min.x > max.x || min.y > max.y) return;

		//Block always inverses
		float difOffset = 1.0f;
		if (whichPad == STATIONARY_PAD) difOffset = -1.0f;

		if (max.x - min.x > max.y - min.y) {
			//wider overlap in x => bounce in y direction:
			if (ball.y > paddle.y) {
				ball.y = paddle.y +(radius.y + ball_radius.y);
				ball_velocity.y =  std::abs(ball_velocity.y);
			} else {
				ball.y = paddle.y - radius.y - ball_radius.y;
				ball_velocity.y = -std::abs(ball_velocity.y);
			}
		} else {
			//wider overlap in y => bounce in x direction:
			if (ball.x > paddle.x) {
				ball.x = paddle.x + (radius.x + ball_radius.x);
				ball_velocity.x = std::abs(ball_velocity.x);
			} else {
				ball.x = paddle.x - radius.x - ball_radius.x;
				ball_velocity.x = -std::abs(ball_velocity.x);
			}
			//warp y velocity based on offset from paddle center:
			float vel = difOffset*(ball.y - paddle.y) / (radius.y + ball_radius.y);
			ball_velocity.y = glm::mix(ball_velocity.y, vel, 0.75f);  //What? 
		}
	};
	paddle_vs_ball(left_paddle, MOVING_PAD);
	paddle_vs_ball(topBlock, STATIONARY_PAD);
	paddle_vs_ball(bottomBlock, STATIONARY_PAD);
	  
	//court walls:
	if (ball.y > court_radius.y - ball_radius.y) {
		ball.y = court_radius.y - ball_radius.y;
		if (ball_velocity.y > 0.0f) {
			ball_velocity.y = -ball_velocity.y;
		}
	}
	if (ball.y < -court_radius.y + ball_radius.y) {
		ball.y = -court_radius.y + ball_radius.y;
		if (ball_velocity.y < 0.0f) {
			ball_velocity.y = -ball_velocity.y;
		}
	}

	if (ball.x > court_radius.x - ball_radius.x) {
		ball.x = court_radius.x - ball_radius.x;
		if (ball_velocity.x > 0.0f) {
			moveBallLeft();
			left_score += 1; 
			newGate(left_score);
		}
	}
	if (gateCollide()) {  //Checks hit with both gates
		ball.x = gateX - ball_radius.x;
		if (ball_velocity.x > 0.0f) {
			left_lives--;
			if (left_lives == 0) gameState = false; //If out of lives, restart the game
			else {
				moveBallLeft();
				newGate(left_score); //Should reset gate even though they lost to avoid cheating
			}
			assert(left_lives > 0 || !gameState);
		}
	}
	if (ball.x < -court_radius.x + ball_radius.x) {
		ball.x = -court_radius.x  + ball_radius.x;
		if (ball_velocity.x < 0.0f) {
			ball_velocity.x = -ball_velocity.x;
		}
	}

	//----- gradient trails -----

	//age up all locations in ball trail:
	for (auto &t : ball_trail) {
		t.z += elapsed;
	}
	//store fresh location at back of ball trail:
	ball_trail.emplace_back(ball, 0.0f);

	//trim any too-old locations from back of trail:
	//NOTE: since trail drawing interpolates between points, only removes back element if second-to-back element is too old:
	while (ball_trail.size() >= 2 && ball_trail[1].z > trail_length) {
		ball_trail.pop_front();
	}
}

void PongMode::draw(glm::uvec2 const &drawable_size) {
	//some nice colors from the course web page:
	#define HEX_TO_U8VEC4( HX ) (glm::u8vec4( (HX >> 24) & 0xff, (HX >> 16) & 0xff, (HX >> 8) & 0xff, (HX) & 0xff ))
	glm::u8vec4 bgCols[10] = { HEX_TO_U8VEC4(0x193b59ff), HEX_TO_U8VEC4(0x038a8aff),
		HEX_TO_U8VEC4(0x5040ffff), HEX_TO_U8VEC4(0xcf8072ff), HEX_TO_U8VEC4(0xbe9640ff),
		HEX_TO_U8VEC4(0x852982ff), HEX_TO_U8VEC4(0x075f1aff), HEX_TO_U8VEC4(0xa8afaaff),
		HEX_TO_U8VEC4(0xb9aee6ff), HEX_TO_U8VEC4(0x000000ff) };
	const glm::u8vec4 fg_color = HEX_TO_U8VEC4(0xf2d2b6ff);
	const glm::u8vec4 block_color = HEX_TO_U8VEC4(0x387f3aff);
	const glm::u8vec4 block_shadow_color = HEX_TO_U8VEC4(0x0d6410ff);
	const glm::u8vec4 shadow_color = HEX_TO_U8VEC4(0xf2ad94ff);
	const std::vector< glm::u8vec4 > trail_colors = {
		HEX_TO_U8VEC4(0xf2ad9488),
		HEX_TO_U8VEC4(0xf2897288),
		HEX_TO_U8VEC4(0xbacac088),
	};
	#undef HEX_TO_U8VEC4

	//other useful drawing constants:
	const float wall_radius = 0.05f;
	const float shadow_offset = 0.07f;
	const float padding = 0.14f; //padding between outside of walls and edge of window

	//---- compute vertices to draw ----

	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

	//inline helper function for rectangle drawing:
	auto draw_rectangle = [&vertices](glm::vec2 const &center, glm::vec2 const &radius, glm::u8vec4 const &color) {
		//draw rectangle as two CCW-oriented triangles:
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));

		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
	};

	//shadows for everything (except the trail):

	glm::vec2 s = glm::vec2(0.0f,-shadow_offset);

	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	draw_rectangle(left_paddle + s, paddle_radius, shadow_color);
	draw_rectangle(topBlock + s, block_radius, block_shadow_color);
	draw_rectangle(bottomBlock + s, block_radius, block_shadow_color);
	if(useEarlier){ //Only draw second gate if after level 10
		draw_rectangle(topCenterB + s, topRadiusB, shadow_color);
		draw_rectangle(bottomCenterB + s, bottomRadiusB, shadow_color);
	}
	draw_rectangle(topCenter + s, topRadius, shadow_color);
	draw_rectangle(bottomCenter + s, bottomRadius, shadow_color);
	draw_rectangle(ball+s, ball_radius, shadow_color);

	//ball's trail:
	if (ball_trail.size() >= 2) {
		//start ti at second element so there is always something before it to interpolate from:
		std::deque< glm::vec3 >::iterator ti = ball_trail.begin() + 1;
		//draw trail from oldest-to-newest:
		constexpr uint32_t STEPS = 20;
		//draw from [STEPS, ..., 1]:
		for (uint32_t step = STEPS; step > 0; --step) {
			//time at which to draw the trail element:
			float t = step / float(STEPS) * trail_length;
			//advance ti until 'just before' t:
			while (ti != ball_trail.end() && ti->z > t) ++ti;
			//if we ran out of recorded tail, stop drawing:
			if (ti == ball_trail.end()) break;
			//interpolate between previous and current trail point to the correct time:
			glm::vec3 a = *(ti-1);
			glm::vec3 b = *(ti);
			glm::vec2 at = (t - a.z) / (b.z - a.z) * (glm::vec2(b) - glm::vec2(a)) + glm::vec2(a);

			//look up color using linear interpolation:
			//compute (continuous) index:
			float c = (step-1) / float(STEPS-1) * trail_colors.size();
			//split into an integer and fractional portion:
			int32_t ci = int32_t(std::floor(c));
			float cf = c - ci;
			//clamp to allowable range (shouldn't ever be needed but good to think about for general interpolation):
			if (ci < 0) {
				ci = 0;
				cf = 0.0f;
			}
			if (ci > int32_t(trail_colors.size())-2) {
				ci = int32_t(trail_colors.size())-2;
				cf = 1.0f;
			}
			//do the interpolation (casting to floating point vectors because glm::mix doesn't have an overload for u8 vectors):
			glm::u8vec4 color = glm::u8vec4(
				glm::mix(glm::vec4(trail_colors[ci]), glm::vec4(trail_colors[ci+1]), cf)
			);

			//draw:
			draw_rectangle(at, ball_radius, color);
		}
	}

	//solid objects:

	//walls:
	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);
	draw_rectangle(topBlock, block_radius, block_color);
	draw_rectangle(bottomBlock, block_radius, block_color);

	//paddle:
	draw_rectangle(left_paddle, paddle_radius, fg_color);

	//gate:
	draw_rectangle(topCenter, topRadius, fg_color); //Top
	draw_rectangle(bottomCenter, bottomRadius, fg_color); //Bottom
	if (useEarlier) {
		draw_rectangle(topCenterB, topRadiusB, fg_color); //Top Before
		draw_rectangle(bottomCenterB, bottomRadiusB, fg_color); //Bottom Before
	}

	//ball:
	draw_rectangle(ball, ball_radius, fg_color);

	//scores:
	glm::vec2 score_radius = glm::vec2(0.1f, 0.1f);
	/*for (uint32_t i = 0; i < left_score; ++i) {
		draw_rectangle(glm::vec2( -court_radius.x + (2.0f + 3.0f * i) * score_radius.x, court_radius.y + 2.0f * wall_radius + 2.0f * score_radius.y), score_radius, fg_color);
	}*/
	for (uint32_t i = 1; i < left_lives; ++i) { //TO DO: Unknown if want to change this
		draw_rectangle(glm::vec2( court_radius.x - (2.0f + 3.0f * i) * score_radius.x, court_radius.y + 2.0f * wall_radius + 2.0f * score_radius.y), score_radius, fg_color);
	}



	//------ compute court-to-window transform ------

	//compute area that should be visible:
	glm::vec2 scene_min = glm::vec2(
		-court_radius.x - 2.0f * wall_radius - padding,
		-court_radius.y - 2.0f * wall_radius - padding
	);
	glm::vec2 scene_max = glm::vec2(
		court_radius.x + 2.0f * wall_radius + padding,
		court_radius.y + 2.0f * wall_radius + 3.0f * score_radius.y + padding
	);

	//compute window aspect ratio:
	float aspect = drawable_size.x / float(drawable_size.y);
	//we'll scale the x coordinate by 1.0 / aspect to make sure things stay square.

	//compute scale factor for court given that...
	float scale = std::min(
		(2.0f * aspect) / (scene_max.x - scene_min.x), //... x must fit in [-aspect,aspect] ...
		(2.0f) / (scene_max.y - scene_min.y) //... y must fit in [-1,1].
	);

	glm::vec2 center = 0.5f * (scene_max + scene_min);

	//build matrix that scales and translates appropriately:
	glm::mat4 court_to_clip = glm::mat4(
		glm::vec4(scale / aspect, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, scale, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(-center.x * (scale / aspect), -center.y * scale, 0.0f, 1.0f)
	);
	//NOTE: glm matrices are specified in *Column-Major* order,
	// so each line above is specifying a *column* of the matrix(!)

	//also build the matrix that takes clip coordinates to court coordinates (used for mouse handling):
	clip_to_court = glm::mat3x2(
		glm::vec2(aspect / scale, 0.0f),
		glm::vec2(0.0f, 1.0f / scale),
		glm::vec2(center.x, center.y)
	);

	//---- actual drawing ----

	//clear the color buffer:
	glm::u8vec4 bg_color = bgCols[(left_score / levelPoints) % 10]; //BG Color is picked for a series in order based on level
	glClearColor(bg_color.r / 255.0f, bg_color.g / 255.0f, bg_color.b / 255.0f, bg_color.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//use alpha blending:
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//don't use the depth test:
	glDisable(GL_DEPTH_TEST);

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program.program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program.OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(court_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, white_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);
	

	GL_ERRORS(); //PARANOIA: print errors just in case we did something wrong.

}
