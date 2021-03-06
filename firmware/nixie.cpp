#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/atomic.h>


class Nixie
{
private:
	static Nixie nixie;

private:
	unsigned char brightness;
	unsigned char hours, minutes;

private:
	static void pushByte(unsigned char data)
	{
		for(signed char i = 7; i >= 0; i--)
		{
			PORTB &= ~0x03;
			PORTB |= (data >> i) & 0x01;
			PORTB |= 0x02;
		}
	}

private:
	explicit Nixie()
		: brightness(0xFF)
		, hours(0xFF)
		, minutes(0xFF)
	{
		// Set PB0--PB3 to output
		DDRB |= 0x0F;

		// Init timer 2
		writeBrightness();
		TCCR2 |= (1 << COM21) | (1 << WGM21) | (1 << WGM20) | (1 << CS21);
	}

	void writeBrightness()
	{
		OCR2 = brightness;
	}
	
	void writeData()
	{
		PORTB &= ~0x04;
		pushByte(hours);
		pushByte(minutes);
		PORTB |= 0x04;
	}

public:
	static void update(const unsigned char hours, const unsigned char minutes)
	{
		if(hours == nixie.hours && minutes == nixie.minutes)
			return;

		nixie.hours = hours;
		nixie.minutes = minutes;
		nixie.writeData();
	}

	static void modifyBrightness(unsigned char (* f)(unsigned char))
	{
		nixie.brightness = f(nixie.brightness);
		nixie.writeBrightness();
	}
};

Nixie Nixie::nixie;


class I2C
{
private:
	static I2C i2c;

private:
	volatile unsigned char busy;
	enum { StateIdle, StateCommand, StateSend, StateRecv } state;
	void (* continuation)();
	unsigned char * buffer;
	unsigned char counter;

private:
	I2C()
		: busy(0)
		, state(StateIdle)
		, continuation(nullptr)
	{
		// Init I2C
		TWBR = 0xC0;
		TWSR = 0;
	}

	bool enter()
	{
		unsigned char tmp = 0;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			tmp = busy;
			busy = 1;
		}
		return (tmp == 0);
	}

private:
	static void sendByte(unsigned char data)
	{
		TWDR = data;
		TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
	}

	static void recvByte(unsigned char left)
	{
		if(left > 1)
			TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWEA) | (1 << TWIE);
		else
			TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWIE);
	}

	static void sendFromBuffer()
	{
		i2c.counter--;
		sendByte(*i2c.buffer++);
	}

public:
	static bool startAsync(void (* cont)())
	{
		if(!i2c.enter())
			return false;
		i2c.state = StateCommand;
		i2c.continuation = cont;
		TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN) | (1 << TWIE);
		return true;
	}

	static bool stopAsync()
	{
		bool res = false;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
		{
			if(i2c.busy == 0)
			{
				TWCR = (1 << TWINT) | (1 << TWEN) | (1 << TWSTO);
				res = true;
			}
		}
		return res;
	}

	static bool recvAsync(void * ptr, unsigned char cnt, void (* cont)())
	{
		if(cnt == 0)
			return true;
		if(!i2c.enter())
			return false;
		i2c.state = StateRecv;
		i2c.continuation = cont;
		i2c.buffer = (unsigned char *)ptr;
		i2c.counter = cnt;
		recvByte(cnt);
		return true;
	}

	static bool sendAsync(void * ptr, unsigned char cnt, void (* cont)())
	{
		if(cnt == 0)
			return true;
		if(!i2c.enter())
			return false;
		i2c.state = StateSend;
		i2c.continuation = cont;
		i2c.buffer = (unsigned char *)ptr;
		i2c.counter = cnt;
		sendFromBuffer();
		return true;
	}

	static void interrupt()
	{
		if(i2c.busy == 0)
			while(1) {}

		switch(i2c.state)
		{
		case StateSend:
			if(i2c.counter > 0)
			{
				sendFromBuffer();
				return;
			}
			break;

		case StateRecv:
			*i2c.buffer++ = TWDR;
			i2c.counter--;
			if(i2c.counter > 0)
			{
				recvByte(i2c.counter);
				return;
			}
			break;

		default:
			break;
		}

		void (* tmp)() = i2c.continuation;
		i2c.continuation = nullptr;
		i2c.state = StateIdle;
		i2c.busy = 0;

		if(tmp != nullptr)
			tmp();
	}
};

I2C I2C::i2c;

ISR(TWI_vect)
{
	I2C::interrupt();
}


volatile unsigned char flags;
const unsigned char TimeChangedFlag = 0x01;


class RTClock
{
public:
	static RTClock rtc;

public:
	unsigned char seconds;
	unsigned char minutes;
	unsigned char hours;
	unsigned char day;
	unsigned char date;
	unsigned char month;
	unsigned char year;
	unsigned char control;

private:
	RTClock() {}

	void readedCallback()
	{
		{
			bool needWrite = false;

			if((seconds & 0x80) != 0)
			{
				setDefault();
				needWrite = true;
			}

			if(control != 0x10)
			{
				control = 0x10;
				needWrite = true;
			}

			if(needWrite)
				writeAsync();
		}

		flags |= TimeChangedFlag;
	}

public:
	void setDefault()
	{
		seconds = 0x00;
		minutes = 0x39;
		hours = 0x13;
		day = 0x3;
		date = 0x28;
		month = 0x10;
		year = 0x13;
	}

public:
	static bool readAsync()
	{
		return I2C::startAsync(contReadSendIndex);
	}

	static bool writeAsync()
	{
		return I2C::startAsync(contWriteSendIndex);
	}

private:
	static void contReadSendIndex()
	{
		static unsigned char addr[] = { 0b11010000, 0x00 };
		I2C::sendAsync(addr, 2, contReadStartRecv);
	}

	static void contReadStartRecv()
	{
		I2C::startAsync(contReadSendAddr);
	}

	static void contReadSendAddr()
	{
		static unsigned char addr[] = { 0b11010001 };
		I2C::sendAsync(addr, 1, contReadRecv);
	}

	static void contReadRecv()
	{
		I2C::recvAsync(&rtc.seconds, 8, contReadStop);
	}

	static void contReadStop()
	{
		I2C::stopAsync();
		rtc.readedCallback();
	}


	static void contWriteSendIndex()
	{
		static unsigned char addr[] = { 0b11010000, 0x00 };
		I2C::sendAsync(addr, 2, contWriteSend);
	}
	
	static void contWriteSend()
	{
		I2C::sendAsync(&rtc, 8, contWriteStop);
	}
	
	static void contWriteStop()
	{
		I2C::stopAsync();
	}
};

RTClock RTClock::rtc;

ISR(INT0_vect)
{
	RTClock::readAsync();
}


class Sensor
{
private:
	static Sensor sensor;

private:
	Sensor()
	{
		// ADC result left adjust
		//ADMUX = (1 << ADLAR);
		//ADCSRA = (1 << ADEN) | (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

		// Timer 1
		//TCNT1 = 0x0000;
		//OCR1A = 0x8000;
		//OCR1B = 0x0080;
		//TCCR1A &= ~(1 << WGM10) & ~(1 << WGM11);
		//TCCR1B = (TCCR1B & ~(1 << CS10) & ~(1 << CS11) & ~(1 << CS12) & ~(1 << WGM13)) | (1 << CS10) | (1 << WGM12);
		//TIMSK = (1 << OCIE1A) | (1 << OCIE1B);
	}

public:
	static void timerAInterrupt()
	{
		DDRC &= 0xF0;
		PORTC &= 0xF0;
	}

	static void timerBInterrupt()
	{
		ADCSRA |= (1 << ADSC);
	}

	static void adcInterrupt()
	{
		/*
		DDRC |= 0x0F;
		PORTC &= 0xF0;
		unsigned char value = ADCH;

		if(clock_flags & POWER_DOWN)
			return;

		const unsigned char SIZE = 4;
		const unsigned char button_id = 0;
		static unsigned short sum = 0;
		static unsigned char head = 0;
		static unsigned char wait = 2;
		static unsigned char buffer[1 << SIZE];
		static unsigned char mean[1 << SIZE];
		static bool button = false;

		if(wait < 2) sum -= buffer[head];
		buffer[head] = value;
		sum += value;
		head = (head + 1) & ((1 << SIZE) - 1);
		mean[head] = (unsigned char)(sum >> SIZE);
		if(head == 0 && wait > 0)
			wait--;

		if(wait == 0)
		{
			unsigned char cur = mean[head];
			unsigned char prev = mean[(head + 1) & ((1 << SIZE) - 1)];
			if(button)
			{
				if(cur - (cur >> 3) > prev)
					button = false;
			}
			else
			{
				if(prev - (prev >> 3) > cur)
					button = true;
			}
		}

		if(button)
			clock_flags |= (1 << button_id);
		else
			clock_flags &= ~(1 << button_id);

		//void setADCInput(unsigned char id)
		//{
		//	ADMUX = (ADMUX & ~0x03) | (id & 0x03);
		//}
		 */
	}
};

Sensor Sensor::sensor;

ISR(TIMER1_COMPA_vect)
{
	Sensor::timerAInterrupt();
}

ISR(TIMER1_COMPB_vect)
{
	Sensor::timerBInterrupt();
}

ISR(ADC_vect)
{
	Sensor::adcInterrupt();
}


class DateTime
{
public:
	static unsigned char isLeapYear(unsigned char year)
	{
		if(year % 4 == 0)
		{
			if(year % 100 == 0)
			{
				if(year % 400 == 0)
					return 1;
				return 0;
			}
			return 1;
		}
		return 0;
	}

	static unsigned char daysInMonth(unsigned char month, unsigned char year)
	{
		const static unsigned char days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		return days[month - 1] + (month == 2 ? isLeapYear(year) : 0);
	}
	
	static unsigned char getDayOfWeek(unsigned char day, unsigned char month, unsigned char year)
	{
		unsigned short a = (14 - month) / 12;
		unsigned short y = 2000 + year - a;
		unsigned short m = month + 12 * a - 2;
		return (7000 + ((unsigned short)day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12)) % 7;
	}
};


int main()
{
	// Enable watchdog
	WDTCR |= (1 << WDCE) | (1 << WDE);
	WDTCR |= (1 << WDE) | (1 << WDP2) | (1 << WDP1) | (1 << WDP0);

	flags = 0;
	sei();

	RTClock::readAsync();

	// Enable INT0 on rising edge
	MCUCR |= (1 << ISC01) | (1 << ISC00);
	GICR |= (1 << INT0);

	for(;;)
	{
		wdt_reset();
		if(flags & TimeChangedFlag)
		{
			Nixie::update(RTClock::rtc.hours, RTClock::rtc.minutes);
			flags &= ~TimeChangedFlag;
		}
	}

	return 0;
}
