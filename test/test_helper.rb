require 'rubygems'
require 'test/unit'
require 'ruby-debug'

require File.expand_path(File.join(File.dirname(__FILE__), 'net_http_multipart_post'))
require File.expand_path(File.join(File.dirname(__FILE__), 'verm_spawner'))

verm_binary = File.join(File.dirname(__FILE__), '..', 'verm')
verm_data   = File.join(File.dirname(__FILE__), 'data')
mime_types_file = File.join(File.dirname(__FILE__), 'fixtures', 'mime.types')
VERM_SPAWNER = VermSpawner.new(verm_binary, verm_data, mime_types_file)
REPLICATION_MASTER_VERM_SPAWNER = VermSpawner.new(verm_binary, verm_data, mime_types_file, VERM_SPAWNER.port + 1, VERM_SPAWNER.host)

module Verm
  class TestCase < Test::Unit::TestCase
    undef_method :default_test if instance_methods.include? 'default_test' or
                                  instance_methods.include? :default_test

    def setup
      VERM_SPAWNER.clear_data
      VERM_SPAWNER.start_verm
      VERM_SPAWNER.wait_until_available
    end
  
    def teardown
      VERM_SPAWNER.stop_verm
    end
    
    def timeout
      10 # seconds
    end
    

    def fixture_file_path(filename)
      File.join(File.dirname(__FILE__), 'fixtures', filename)
    end

    def get(options)
      verm_spawner = options.delete(:verm) || VERM_SPAWNER
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      
      response = http.get(options[:path], options[:headers])
      
      assert_equal options[:expected_response_code] || 200, response.code.to_i, "The response didn't have the expected code"
      assert_equal options[:expected_content_type], response.content_type, "The response had an incorrect content-type" if options.has_key?(:expected_content_type)
      assert_equal options[:expected_content_length], response.content_length, "The response had an incorrect content-length" if options.has_key?(:expected_content_length)
      assert_equal options[:expected_content_encoding], response['content-encoding'], "The response had an incorrect content-encoding" if options.has_key?(:expected_content_encoding)
      assert_equal options[:expected_content], response.body, "The response had incorrect content" if options.has_key?(:expected_content)
      
      response
    end
    
    def post_file(options)
      verm_spawner = options.delete(:verm) || VERM_SPAWNER
      orig_filename = fixture_file_path(options[:file])
      file_data = File.read(orig_filename)
      
      if @raw
        request = Net::HTTP::Post.new(options[:path])
        request.content_type = options[:type]
        request['Content-Encoding'] = options[:encoding] if options[:encoding]
      else
        request = Net::HTTP::MultipartPost.new(options[:path])
        request.attach 'uploaded_file', file_data, options[:file], options[:type]
        request.form_data = {"test" => "bar"}
      end
      
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      location = http.start do |connection|
        if @raw
          response = connection.request(request, file_data)
        else
          response = connection.request(request)
        end
        response['location']
      end

      raise "The location returned was #{location}, but it was supposed to be saved under #{options[:path]}/" if location[0..options[:path].length] != "#{options[:path]}/"
      raise "The location returned was #{location}, but it was supposed to have a #{options[:expected_extension]} extension" if options[:expected_extension] && location[(-options[:expected_extension].length - 1)..-1] != ".#{options[:expected_extension]}"
      dest_filename = File.expand_path(File.join(verm_spawner.verm_data, location))
      dest_filename += '.' + options[:expected_extension_suffix] if options[:expected_extension_suffix]
      raise "Verm supposedly saved the file to #{dest_filename}, but that doesn't exist" unless File.exist?(dest_filename)
      saved_data = File.read(dest_filename)
      raise "The data saved to file doesn't match the original! #{saved_data.inspect} vs. #{file_data.inspect}" unless saved_data == file_data
      location
    end
    
    def put_file(options, verm_spawner = VERM_SPAWNER)
      orig_filename = fixture_file_path(options[:file])
      file_data = File.read(orig_filename)
      
      request = Net::HTTP::Put.new(options[:path])
      request.content_type = options[:type]
      request['Content-Encoding'] = options[:encoding] if options[:encoding]
      
      http = Net::HTTP.new(verm_spawner.hostname, verm_spawner.port)
      http.read_timeout = timeout
      location = http.start do |connection|
        response = connection.request(request, file_data)
        response.error! unless response.is_a?(Net::HTTPSuccess)
        response['location']
      end

      raise "The location returned was #{location}, but it was supposed to be the requested location #{options[:path]}" if location != options[:path]
      dest_filename = File.expand_path(File.join(verm_spawner.verm_data, location))
      dest_filename += '.' + options[:expected_extension_suffix] if options[:expected_extension_suffix]
      raise "Verm supposedly saved the file to #{dest_filename}, but that doesn't exist" unless File.exist?(dest_filename)
      saved_data = File.read(dest_filename)
      raise "The data saved to file doesn't match the original! #{saved_data.inspect} vs. #{file_data.inspect}" unless saved_data == file_data
      dest_filename
    end


    def get_statistics(options = {})
      response = get(options.merge(:path => "/_statistics", :expected_response_code => 200))
      lines = response.body.split(/\n/)
      lines.inject({}) {|results, line| name, value = line.split(/ /); results[name.to_sym] = value.to_i; results}
    end

    def calculate_statistics_change(before, after)
      results = after.inject({}) {|results, (k, v)| results[k] = v - before[k] unless v == before[k]; results}
    end

    def assert_statistics_change(expected_change, options = {})
      before = get_statistics(options)
      yield
      after = get_statistics(options)

      change = calculate_statistics_change(before, after)
      assert_equal expected_change, change
    end
  end
end
