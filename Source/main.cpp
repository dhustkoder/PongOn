#include <cmath>
#include <cassert>
#include <cstring>
#include <cstdint>

#include <iostream>
#include <string>
#include <vector>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

constexpr const unsigned int WinWidth {512};
constexpr const unsigned int WinHeight {256};

struct Position {
	float top, bottom, left, right;
};

struct Ball : sf::CircleShape {
	static constexpr const float Radius {10.5f};
	static constexpr const float Velocity {2.5f};
	Ball() : sf::CircleShape(Radius) {
		setPosition(WinWidth / 2, WinHeight / 2);
		setOrigin(Radius, Radius);
		setFillColor(sf::Color::Green);
		setOutlineColor(sf::Color::Magenta);
	}
};

struct Paddle : sf::RectangleShape {
	static constexpr const float Width {15.f};
	static constexpr const float Height {60.f};
	static constexpr const float Velocity {8.8f};
	Paddle() : sf::RectangleShape({Width, Height}) {
		setOrigin(Width / 2, Height / 2);
		setFillColor(sf::Color::Red);
		setOutlineColor(sf::Color::Green);
	}
};

struct Shapes {
	Ball ball;
	Paddle local;
	Paddle remote;
};

struct Velocities {
	sf::Vector2f ball {Ball::Velocity, Ball::Velocity / 4};
	float local {0.f};
	float remote {0.f};
};

struct Positions {
	Position ball;
	Position local;
	Position remote;
};

namespace Connection {
	enum class Mode {Server, Client};
	static constexpr const unsigned short Port {7171};
	static sf::TcpSocket socket;
	static std::string local_nick;
	static std::string remote_nick;
	static std::size_t bytes_received;
	static sf::Socket::Status status;
	static bool is_server;

	static bool Init(Mode mode);
	static bool Exchange(sf::Packet* send, sf::Packet* receive);
	template<class Data>
	bool Exchange(Data sending, Data* receiving);
	template<class SendFunc, class ReceiveFunc>
	bool ExchangeFun(SendFunc send, ReceiveFunc receive);
	template<class ...Args>
	bool Send(Args&& ...args);
	template<class ...Args>
	bool Receive(Args&& ...args);
}

static void update_positions(const Shapes& shapes, Positions* positions);
static void update_velocities(const Positions& positions, Velocities* velocities);
static void update_shapes(const Velocities& velocities, Shapes* shapes);
static void process_input(sf::Keyboard::Key code, bool pressed, Velocities* velocities);


int main(int argc, char** argv)
{
	if (argc > 1) {
		if (std::strcmp(argv[1], "-server") == 0) {
			if(!Connection::Init(Connection::Mode::Server))
				return EXIT_FAILURE;
		} else if (std::strcmp(argv[1], "-client") == 0) {
			if(!Connection::Init(Connection::Mode::Client))
				return EXIT_FAILURE;
		} else {
			std::cerr << "unknown argument: " << argv[1] << '\n';
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
	sf::RenderWindow window({WinWidth, WinHeight}, "PongOn");
	sf::Event event;

	if (Connection::is_server) {
		shapes.local.setPosition({Paddle::Width/2, WinHeight/2});
		shapes.remote.setPosition({WinWidth - Paddle::Width/2, WinHeight/2});
	} else {
		shapes.local.setPosition({WinWidth - Paddle::Width/2, WinHeight/2});
		shapes.remote.setPosition({Paddle::Width/2, WinHeight/2});
	}

	window.setFramerateLimit(60);
	while (window.isOpen()) {
		while (window.pollEvent(event)) {
			switch (event.type) {
			case sf::Event::KeyPressed:
				process_input(event.key.code, true, &velocities);
				break;
			case sf::Event::KeyReleased:
				process_input(event.key.code, false, &velocities);
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
		
		if (!Connection::Exchange(velocities.local, &velocities.remote)) {
			std::cerr << "Connection error: " << Connection::status << '\n';
			break;
		}
		
		update_shapes(velocities, &shapes);
		
		window.clear(sf::Color::Blue);
		window.draw(shapes.ball);
		window.draw(shapes.local);
		window.draw(shapes.remote);
		window.display();
	}

	return EXIT_SUCCESS;
}




void update_positions(const Shapes& shapes, Positions* const positions)
{
	const auto update =
	[](const sf::Shape& shape, Position& pos, float width, float height) {
		const auto width_diff = width / 2.f;
		const auto height_diff = height / 2.f;
		const auto shapePos = shape.getPosition();
		pos.right = shapePos.x + width_diff;
		pos.left = shapePos.x - width_diff;
		pos.bottom = shapePos.y + height_diff;
		pos.top = shapePos.y - height_diff;
	};
	update(shapes.ball, positions->ball, Ball::Radius, Ball::Radius);
	update(shapes.local, positions->local, Paddle::Width, Paddle::Height);
	update(shapes.remote, positions->remote, Paddle::Width, Paddle::Height);
}


void update_velocities(const Positions& positions, Velocities* const velocities)
{	
	const auto& ballpos = positions.ball;
	auto& ballvel = velocities->ball;
	
	const auto collided = [&ballpos](const auto& paddle) {
		return (ballpos.right >= paddle.left && ballpos.left <= paddle.right)
		&& (ballpos.bottom >= paddle.top && ballpos.top <= paddle.bottom);
	};
	
	if (collided(positions.local) || collided(positions.remote)) {
		ballvel.x = -ballvel.x;
	} else {
		if (ballpos.left < 0)
			ballvel.x = std::abs(ballvel.x);
		else if (ballpos.right > WinWidth)
			ballvel.x = -std::abs(ballvel.x);

		if (ballpos.top < 0)
			ballvel.y = std::abs(ballvel.y);
		else if (ballpos.bottom > WinHeight)
			ballvel.y = -std::abs(ballvel.y);
	}
		
	if (velocities->local) {
		const auto& pos = positions.local;
		auto& vel = velocities->local;
		if (vel < 0 && pos.top <= 0)
			vel = 0;
		else if (vel > 0 && pos.bottom >= WinHeight)
			vel = 0;
	}
}

void update_shapes(const Velocities& velocities, Shapes* const shapes)
{
	if (velocities.ball != sf::Vector2f{0, 0})
		shapes->ball.setPosition(shapes->ball.getPosition() + velocities.ball);
	
	if (velocities.local) {
		const auto& pos = shapes->local.getPosition();
		shapes->local.setPosition(pos.x, pos.y + velocities.local);
	}
	
	if (velocities.remote) {
		const auto& pos = shapes->remote.getPosition();
		shapes->remote.setPosition(pos.x, pos.y + velocities.remote);
	}
}


void process_input(const sf::Keyboard::Key code, const bool pressed, Velocities* const velocities)
{
	float& vel = velocities->local;
	if (pressed) {
		switch (code) {
		case sf::Keyboard::W: vel = -Paddle::Velocity; break;
		case sf::Keyboard::S: vel = Paddle::Velocity; break;
		default: vel = 0; break;
		}
	} else {
		vel = 0;
	}
}


bool Connection::Init(const Mode mode)
{
	is_server = mode == Mode::Server;
	do {
		std::cout << "enter your nickname: ";
		std::getline(std::cin, local_nick);
	} while (local_nick.size() == 0);
	
	if (local_nick.size() > 10)
		local_nick.resize(10);

	if (is_server) {
		sf::TcpListener listener;
		std::cout << "booting as server...\n";
		if (listener.listen(Port) != sf::Socket::Done) {
			std::cerr << "failed to listen port " << Port << '\n';
			return false;
		}
		
		std::cout << "waiting for client...\n"; 
		if (listener.accept(socket) != sf::Socket::Done) {
			std::cerr << "connection failed\n";
			return false;
		}
	} else {
		sf::IpAddress serverIp;
		std::cout << "booting as client...\n";
		std::cout << "enter the server\'s ip address: ";
		std::cin >> serverIp;
		if (socket.connect(serverIp, Port) != sf::Socket::Done) {
			std::cerr << "connection failed!\n";
			return false;
		}
	}

	sf::Packet send_pack, receive_pack;
	send_pack << local_nick;
	
	if (!Exchange(&send_pack, &receive_pack)) {
		std::cerr << "failed to exchange nicks\n";
		return false;
	}

	receive_pack >> remote_nick;
	std::cout << "connected to: " << remote_nick << '\n';

	return true;
}

template<class ...Args>
bool Connection::Send(Args&& ...args)
{
	status = socket.send(std::forward<Args>(args)...);
	return status == sf::Socket::Done;
}

template<class ...Args>
bool Connection::Receive(Args&& ...args)
{
	status = socket.receive(std::forward<Args>(args)...);
	return status == sf::Socket::Done;
}

bool Connection::Exchange(sf::Packet* const send, sf::Packet* const receive)
{
	return ExchangeFun([=]{return Send(*send);},
			[=]{return Receive(*receive);});
}

template<class Data>
bool Connection::Exchange(const Data sending, Data* const receiving) 
{
	return ExchangeFun([=]{return Send(&sending, sizeof(Data));},
                        [=]{return Receive(receiving, sizeof(Data), bytes_received);});
}

template<class SendFunc, class ReceiveFunc>
bool Connection::ExchangeFun(const SendFunc send, const ReceiveFunc receive)
{
	if (is_server)
		return send() && receive();
	else
		return receive() && send();
}

