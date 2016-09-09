#include <cmath>
#include <cstring>
#include <iostream>
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

constexpr const int WIN_WIDTH {512};
constexpr const int WIN_HEIGHT {256};
constexpr const int CONNECTION_PORT {7171};

struct Position {
	float top, bottom, left, right;
};

struct Ball : sf::CircleShape {
	static constexpr const float RADIUS {10.5};
	static constexpr const float VELOCITY {7.5};
	Ball() : sf::CircleShape(RADIUS) {
		setPosition(WIN_WIDTH / 2, WIN_HEIGHT / 2);
		setOrigin(RADIUS, RADIUS);
	}
};

struct Paddle : sf::RectangleShape {
	static constexpr const float WIDTH {15};
	static constexpr const float HEIGHT {60};
	static constexpr const float VELOCITY {8.8};
	Paddle(sf::Vector2f pos) : sf::RectangleShape({WIDTH, HEIGHT}) {
		setPosition(pos);
		setOrigin(WIDTH / 2, HEIGHT / 2);
	}
};

struct Shapes {
	Ball ball;
	Paddle player {{Paddle::WIDTH, WIN_HEIGHT / 2}};
	Paddle cpu {{WIN_WIDTH - Paddle::WIDTH, WIN_HEIGHT / 2}};
};

struct Velocities {
	sf::Vector2f ball {Ball::VELOCITY, Ball::VELOCITY / 4};
	float player {0};
	float cpu {0};
};

struct Positions {
	Position ball;
	Position player;
	Position cpu;
};

static sf::TcpSocket tcp_socket;
static bool is_server = false;


static bool boot_as_server();
static bool boot_as_client();
static void update_positions(const Shapes& shapes, Positions* positions);
static void update_velocities(const Positions& positions, Velocities* velocities);
static void update_shapes(const Velocities& velocities, Shapes* shapes);
static void process_input(Velocities* velocities);


int main(int argc, char** argv)
{
	if (argc > 1) {
		if (std::strcmp(argv[1], "-server") == 0) {
			if(!boot_as_server())
				return EXIT_FAILURE;
		} else if (std::strcmp(argv[1], "-client") == 0) {
			if(!boot_as_client())
				return EXIT_FAILURE;
		}
	} else {
		std::cerr << "usage: " << argv[0] << " <mode>\n"
		          << "mode: -server, -client\n";
		return EXIT_FAILURE;
	}

	Shapes shapes;
	Positions positions;
	Velocities velocities;
	sf::RenderWindow window({WIN_WIDTH, WIN_HEIGHT}, "PongCpp");
	sf::Event event;
	
	window.setFramerateLimit(60);
	
	while (window.isOpen()) {
		while (window.pollEvent(event)) {
			switch (event.type) {
			case sf::Event::KeyPressed: /*fall*/
			case sf::Event::KeyReleased:
				process_input(&velocities);
				break;
			case sf::Event::Closed:
				window.close();
				break;
			default:
				break;
			}
		}
		
		update_positions(shapes, &positions);
		update_velocities(positions, &velocities);
		update_shapes(velocities, &shapes);
		
		window.clear(sf::Color::Red);
		window.draw(shapes.ball);
		window.draw(shapes.player);
		window.draw(shapes.cpu);
		window.display();
	}

	return EXIT_SUCCESS;
}




void update_positions(const Shapes& shapes, Positions* const positions)
{
	const auto update =
	[](const sf::Shape& shape, Position& pos, float width, float height) {
		const auto shapePos = shape.getPosition();
		const auto width_diff = width / 2.f;
		const auto height_diff = height / 2.f;
		pos.right = shapePos.x + width_diff;
		pos.left = shapePos.x - width_diff;
		pos.bottom = shapePos.y + height_diff;
		pos.top = shapePos.y - height_diff;
	};
	
	update(shapes.ball, positions->ball, Ball::RADIUS, Ball::RADIUS);
	update(shapes.player, positions->player, Paddle::WIDTH, Paddle::HEIGHT);
	update(shapes.cpu, positions->cpu, Paddle::WIDTH, Paddle::HEIGHT);
}


void update_velocities(const Positions& positions, Velocities* const velocities)
{
	using std::abs;
	
	const auto& ballPos = positions.ball;
	auto& ballVel = velocities->ball;
	
	const auto collided = [&ballPos](const auto& paddle) {
		return (ballPos.right >= paddle.left && ballPos.left <= paddle.right)
		&& (ballPos.bottom >= paddle.top && ballPos.top <= paddle.bottom);
	};
	
	if (collided(positions.player) || collided(positions.cpu))
		ballVel.x = -ballVel.x;
	else if (ballPos.left < 0)
		ballVel.x = abs(ballVel.x);
	else if (ballPos.right > WIN_WIDTH)
		ballVel.x = -abs(ballVel.x);
	
	if (ballPos.top < 0)
		ballVel.y = abs(ballVel.y);
	else if (ballPos.bottom > WIN_HEIGHT)
		ballVel.y = -abs(ballVel.y);
		
	const auto check_vel = [](const Position& pos, float& vel) {
		if (!vel)
			return;
		else if (vel < 0 && pos.top <= 0)
			vel = 0;
		else if (vel > 0 && pos.bottom >= WIN_HEIGHT)
			vel = 0;
	};
	
	const auto send_vel = [](float vel) {
		if (tcp_socket.send(&vel, sizeof(vel)) != sf::Socket::Done)
		    std::cerr << "failed to send data!\n";
	};
	
	const auto receive_vel = [](float& vel) {
		std::size_t dummy;
		if (tcp_socket.receive(&vel, sizeof(vel), dummy) != sf::Socket::Done)
		    std::cerr << "failed to receive data!\n";
	};
	
	if (is_server) {
		const auto& local_pos = positions.player;
		auto& local_vel = velocities->player;
		auto& client_vel = velocities->cpu;
		check_vel(local_pos, local_vel);
		send_vel(local_vel);
		receive_vel(client_vel);
	} else {
		const auto& local_pos = positions.cpu;
		auto& local_vel = velocities->cpu;
		auto& server_vel = velocities->player;
		check_vel(local_pos, local_vel);
		receive_vel(server_vel);
		send_vel(local_vel);
	}
}


void update_shapes(const Velocities& velocities, Shapes* const shapes)
{
	if (velocities.ball != sf::Vector2f{0, 0})
		shapes->ball.setPosition(shapes->ball.getPosition() + velocities.ball);
	
	if (velocities.player) {
		const auto& pos = shapes->player.getPosition();
		shapes->player.setPosition(pos.x, pos.y + velocities.player);
	}
	
	if (velocities.cpu) {
		const auto& pos = shapes->cpu.getPosition();
		shapes->cpu.setPosition(pos.x, pos.y + velocities.cpu);
	}
}


void process_input(Velocities* const velocities)
{
	float& vel = is_server ? velocities->player : velocities->cpu;
	if (sf::Keyboard::isKeyPressed(sf::Keyboard::W))
	    vel = -Paddle::VELOCITY;
	else if (sf::Keyboard::isKeyPressed(sf::Keyboard::S))
		vel = Paddle::VELOCITY;
	else
		vel = 0;
}



bool boot_as_server()
{
	sf::TcpListener listener;
	is_server = true;
	
	std::cout << "booting as server...\n";
	
	if (listener.listen(CONNECTION_PORT) == sf::Socket::Done) {
		std::cout << "waiting for client...\n"; 
		listener.accept(tcp_socket);
		std::cout << "client connected from: " << tcp_socket.getRemoteAddress() << '\n';
	} else {
		std::cerr << "failed to listen port " << CONNECTION_PORT << '\n';
		return false;
	}
	
	return true;
}

bool boot_as_client()
{
	sf::IpAddress ip;
	std::cout << "booting as client...\n";
	std::cout << "enter the server ip address: ";
	std::cin >> ip;
	
	if (tcp_socket.connect(ip, CONNECTION_PORT) == sf::Socket::Done) {
		std::cout << "connetion succed!\n";
	} else {
		std::cerr << "connection failed!\n";
		return false;
	}
	
	return true;
}



